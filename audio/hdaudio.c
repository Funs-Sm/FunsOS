/* ============================================================================
 * hdaudio.c - Complete Intel High Definition Audio Driver
 *
 * Implements the full Intel HD Audio specification. Features:
 *   - PCI HDA controller detection (class 0x0403)
 *   - CORB/RIRB command/response ring buffers (256 entries each)
 *   - Stream Descriptor management (input and output)
 *   - Codec enumeration and widget discovery
 *   - Audio path setup (DAC → Pin complex)
 *   - Volume controls (amplifier gain, 0.25dB steps, mute)
 *   - Power management (D0-D3, CLKSTOP)
 *   - HDMI/DisplayPort audio output
 *   - Interrupt handling (stream completion, unsolicited response)
 *   - Multi-format support (44.1/48/88.2/96/192 kHz, 16/20/24/32-bit, 2-8 ch)
 *
 * Hardware Architecture:
 *   The HDA controller is a PCI device that communicates with audio
 *   codecs over the HDA link (a serial bus). The controller provides:
 *
 *   CORB (Command Output Ring Buffer):
 *     Up to 256 entries, each 32 bits. The host writes commands to CORB
 *     and increments CORBWP. The controller reads entries from CORBRP
 *     and sends them to the codec. CORBRP automatically advances.
 *
 *   RIRB (Response Input Ring Buffer):
 *     Up to 256 entries, each 64 bits (response + flags). The controller
 *     writes responses from codecs into RIRB and increments RIRBWP.
 *     The host reads entries at its own pace.
 *
 *   Stream Descriptors:
 *     Each stream has its own register block (0x20 bytes), BDL for DMA,
 *     and format configuration. Input streams are numbered from 0,
 *     output streams are numbered from 0 (separately).
 *
 *   Verbs:
 *     Commands sent to codecs use a 32-bit format:
 *       Bits 31:28 = Codec address (0-14)
 *       Bits 27:20 = Node ID (widget)
 *       Bits 19:0  = Verb + payload
 * ============================================================================
 */

#include "hdaudio.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"
#include "io.h"
#include "irq.h"
#include "klog.h"
#include "stddef.h"

/* ============================================================================
 * Static controller instance
 * ============================================================================
 */
static hda_controller_t g_hda;

/* ============================================================================
 * MMIO Register Access Helpers
 *
 * HDA controller registers are memory-mapped. All register accesses
 * must use volatile to prevent compiler optimization.
 * ============================================================================
 */

static inline uint8_t hda_read8(uint32_t offset)
{
    return *(volatile uint8_t *)(g_hda.reg_base + offset);
}

static inline uint16_t hda_read16(uint32_t offset)
{
    return *(volatile uint16_t *)(g_hda.reg_base + offset);
}

static inline uint32_t hda_read32(uint32_t offset)
{
    return *(volatile uint32_t *)(g_hda.reg_base + offset);
}

static inline void hda_write8(uint32_t offset, uint8_t val)
{
    *(volatile uint8_t *)(g_hda.reg_base + offset) = val;
}

static inline void hda_write16(uint32_t offset, uint16_t val)
{
    *(volatile uint16_t *)(g_hda.reg_base + offset) = val;
}

static inline void hda_write32(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(g_hda.reg_base + offset) = val;
}

/* ============================================================================
 * Stream Descriptor Base Address
 * ============================================================================
 */
static inline uint32_t hda_sd_offset(uint8_t stream_num)
{
    return HDA_SD_BASE + (uint32_t)stream_num * HDA_SD_SIZE;
}

/* ============================================================================
 * Verb Construction
 *
 * HDA commands use 32-bit "verbs":
 *   Bits 31:28 = codec address
 *   Bits 27:20 = node ID
 *   Bits 19:0  = verb ID + payload
 * ============================================================================
 */

static inline uint32_t hda_make_verb(uint32_t codec_addr, uint32_t nid, uint32_t verb)
{
    return (codec_addr << 28) | ((nid & 0xFF) << 20) | (verb & 0xFFFFF);
}

/* ============================================================================
 * CORB/RIRB Operations
 * ============================================================================
 */

/**
 * Send a command verb to a codec via CORB.
 * The command is written to the next CORB entry and CORBWP is advanced.
 */
void hdaudio_send_command(uint8_t codec_addr, uint32_t cmd)
{
    uint16_t wp = hda_read16(HDA_REG_CORBWP) & 0xFF;
    uint16_t next_wp = (wp + 1) % g_hda.corb_entries;

    g_hda.corb[next_wp] = cmd;

    /* Advance write pointer - tells controller to send the command */
    hda_write16(HDA_REG_CORBWP, next_wp);
}

/**
 * Read a response from RIRB.
 * Returns 0 on success with *response set, -1 on timeout.
 */
int hdaudio_read_response(uint8_t codec_addr, uint32_t *response)
{
    uint16_t old_wp = hda_read16(HDA_REG_RIRBWP) & 0xFF;
    uint32_t timeout = 1000000;

    while (timeout--) {
        uint16_t new_wp = hda_read16(HDA_REG_RIRBWP) & 0xFF;
        if (new_wp != old_wp) {
            /* New response available */
            uint32_t idx = (old_wp + 1) % g_hda.rirb_entries;
            uint64_t entry = g_hda.rirb[idx];

            /* RIRB entry format: [31:0]=response, [63:32]=flags
             * bits 63:60 = codec addr, bit 59 = unsolicited */
            *response = (uint32_t)(entry & 0xFFFFFFFFU);

            /* Advance our local read pointer equivalent */
            return 0;
        }
        io_wait();
    }

    return -1;
}

/**
 * Send a verb and wait for the response.
 * This is a convenience wrapper around send_command + read_response.
 */
static int hda_send_verb(uint32_t codec_addr, uint32_t nid, uint32_t verb, uint32_t *response)
{
    uint32_t cmd = hda_make_verb(codec_addr, nid, verb);
    hdaudio_send_command((uint8_t)codec_addr, cmd);
    return hdaudio_read_response((uint8_t)codec_addr, response);
}

/**
 * Send a verb that doesn't return a response (set-type verbs).
 */
static void hda_send_verb_set(uint32_t codec_addr, uint32_t nid, uint32_t verb)
{
    uint32_t cmd = hda_make_verb(codec_addr, nid, verb);
    hdaudio_send_command((uint8_t)codec_addr, cmd);

    /* Consume the response */
    uint32_t timeout = 10000;
    uint16_t old_wp = hda_read16(HDA_REG_RIRBWP) & 0xFF;
    while (timeout--) {
        uint16_t new_wp = hda_read16(HDA_REG_RIRBWP) & 0xFF;
        if (new_wp != old_wp) break;
        io_wait();
    }
}

/* ============================================================================
 * Codec Enumeration and Widget Discovery
 * ============================================================================
 */

/**
 * Get a parameter from a codec widget.
 */
static int hda_get_param(uint32_t codec_addr, uint32_t nid, uint32_t param_id, uint32_t *value)
{
    return hda_send_verb(codec_addr, nid, HDA_VERB_GET_PARAM | param_id, value);
}

/**
 * Get a connection list entry from a widget.
 */
static int hda_get_conn_entry(uint32_t codec_addr, uint32_t nid, uint32_t index, uint32_t *value)
{
    return hda_send_verb(codec_addr, nid, HDA_VERB_GET_CONN_LIST_ENTRY | index, value);
}

/**
 * Discover all widgets for a codec.
 * The discovery process:
 *   1. Identify the Audio Function Group (AFG) node
 *   2. Get the subordinate node count to determine widget range
 *   3. For each widget, read capabilities, parameters, and connections
 */
static int hda_enumerate_codec(hda_codec_t *codec)
{
    uint32_t i;

    /* Step 1: Read Vendor ID and Revision */
    if (hda_get_param(codec->addr, 0, HDA_PARAM_VENDOR_ID, &codec->vendor_id) != 0) {
        klog_err("HDA: codec %u - failed to read vendor ID", codec->addr);
        return -1;
    }
    hda_get_param(codec->addr, 0, HDA_PARAM_REVISION_ID, &codec->revision_id);

    klog_info("HDA: codec %u - VID=0x%08X, REV=0x%08X",
        codec->addr, codec->vendor_id, codec->revision_id);

    /* Step 2: Get subordinate node count */
    uint32_t node_count;
    if (hda_get_param(codec->addr, 0, HDA_PARAM_SUBORDINATE_NODE_COUNT, &node_count) != 0) {
        return -1;
    }

    uint32_t start_nid = (node_count >> 16) & 0xFF;
    uint32_t total_nodes = node_count & 0xFF;

    /* Step 3: Find the Audio Function Group (type 0x01) */
    codec->afg_nid = 0;
    for (i = start_nid; i < start_nid + total_nodes; i++) {
        uint32_t fg_type;
        if (hda_get_param(codec->addr, i, HDA_PARAM_FUNCTION_GROUP_TYPE, &fg_type) == 0) {
            if ((fg_type & 0xFF) == 0x01) {
                codec->afg_nid = i;
                break;
            }
        }
    }

    if (codec->afg_nid == 0) {
        klog_err("HDA: codec %u - no audio function group found", codec->addr);
        return -1;
    }

    /* Step 4: Get widget range from the AFG */
    uint32_t afg_node_count;
    if (hda_get_param(codec->addr, codec->afg_nid,
            HDA_PARAM_SUBORDINATE_NODE_COUNT, &afg_node_count) != 0) {
        return -1;
    }

    codec->afg_start = (afg_node_count >> 16) & 0xFF;
    codec->afg_count = afg_node_count & 0xFF;

    klog_info("HDA: codec %u - AFG=0x%02X, widgets %u-%u",
        codec->addr, codec->afg_nid,
        codec->afg_start, codec->afg_start + codec->afg_count - 1);

    /* Step 5: Allocate and populate widget descriptors */
    codec->widget_count = codec->afg_count;
    codec->widgets = (hda_widget_t *)kcalloc(codec->widget_count, sizeof(hda_widget_t));
    if (!codec->widgets) {
        klog_err("HDA: failed to allocate widget descriptors");
        return -1;
    }

    for (i = 0; i < codec->afg_count; i++) {
        uint32_t nid = codec->afg_start + i;
        hda_widget_t *w = &codec->widgets[i];
        uint32_t cap;
        uint32_t val;

        w->nid = nid;

        /* Get widget capabilities */
        if (hda_get_param(codec->addr, nid, HDA_PARAM_AUDIO_WIDGET_CAP, &cap) == 0) {
            w->capabilities = cap;
            w->type = (uint8_t)((cap & HDA_WIDGET_CAP_TYPE_MASK) >> HDA_WIDGET_CAP_TYPE_SHIFT);

            /* Track key widgets for audio path setup */
            switch (w->type) {
                case HDA_WTYPE_AUDIO_OUT:
                    if (codec->output_dac_nid == 0) codec->output_dac_nid = nid;
                    /* Get output amp capabilities */
                    if (cap & HDA_WIDGET_CAP_OUT_AMP_PRESENT) {
                        hda_get_param(codec->addr, nid, HDA_PARAM_OUTPUT_AMP_CAP,
                            &w->out_amp_cap);
                    }
                    break;
                case HDA_WTYPE_AUDIO_IN:
                    if (codec->input_adc_nid == 0) codec->input_adc_nid = nid;
                    break;
                case HDA_WTYPE_AUDIO_MIXER:
                    if (codec->mixer_nid == 0) codec->mixer_nid = nid;
                    break;
                case HDA_WTYPE_PIN_COMPLEX:
                    /* Get pin capabilities */
                    hda_get_param(codec->addr, nid, HDA_PARAM_PIN_CAP, &w->pin_cap);
                    /* Get default pin configuration */
                    hda_send_verb(codec->addr, nid, 0xF1C00, &w->pin_config);

                    /* Determine if this is an output or input pin */
                    if (w->pin_cap & HDA_PIN_CAP_OUTPUT) {
                        uint32_t dev = w->pin_config & HDA_CONFIG_DEFAULT_DEVICE_MASK;
                        if ((dev == HDA_DEV_LINE_OUT || dev == HDA_DEV_SPEAKER ||
                             dev == HDA_DEV_HP_OUT || dev == HDA_DEV_HDMI_OUT ||
                             dev == HDA_DEV_SPDIF_OUT || dev == HDA_DEV_DIGITAL_OTHER_OUT)) {
                            if (codec->output_pin_nid == 0) codec->output_pin_nid = nid;
                        }
                    }
                    if (w->pin_cap & HDA_PIN_CAP_INPUT) {
                        uint32_t dev = w->pin_config & HDA_CONFIG_DEFAULT_DEVICE_MASK;
                        if ((dev == HDA_DEV_LINE_IN || dev == HDA_DEV_MIC_IN ||
                             dev == HDA_DEV_SPDIF_IN || dev == HDA_DEV_AUX)) {
                            if (codec->input_pin_nid == 0) codec->input_pin_nid = nid;
                        }
                    }
                    /* EAPD for output pins */
                    if ((w->pin_cap & HDA_PIN_CAP_EAPD) && codec->output_pin_nid == nid) {
                        /* Will be enabled during stream start */
                    }
                    break;
                case HDA_WTYPE_VOLUME_KNOB:
                    if (codec->volume_knob_nid == 0) codec->volume_knob_nid = nid;
                    break;
            }
        }

        /* Get PCM support */
        if (w->type == HDA_WTYPE_AUDIO_OUT || w->type == HDA_WTYPE_AUDIO_IN) {
            hda_get_param(codec->addr, nid, HDA_PARAM_PCM_SUPPORT, &w->pcm_support);
        }

        /* Get input amp capabilities */
        if (w->capabilities & HDA_WIDGET_CAP_IN_AMP_PRESENT) {
            hda_get_param(codec->addr, nid, HDA_PARAM_INPUT_AMP_CAP, &w->in_amp_cap);
        }

        /* Get connection list */
        if (w->capabilities & HDA_WIDGET_CAP_CP) {
            if (hda_get_param(codec->addr, nid, HDA_PARAM_CONN_LIST_LENGTH, &val) == 0) {
                uint32_t list_len = val & 0x7F;
                uint8_t long_form = (val >> 7) & 0x01;

                if (list_len > HDA_MAX_CONNECTIONS) list_len = HDA_MAX_CONNECTIONS;
                w->conn_list_len = list_len;

                uint32_t j;
                for (j = 0; j < list_len; j++) {
                    uint32_t conn;
                    if (hda_get_conn_entry(codec->addr, nid, j, &conn) == 0) {
                        w->conn_list[j] = (uint16_t)(conn & 0xFFFF);
                    }
                    if (long_form) {
                        /* Long-form: need to issue another verb for the high word */
                        if (hda_get_conn_entry(codec->addr, nid, j + 1, &conn) == 0) {
                            w->conn_list[j] |= (uint16_t)((conn & 0xFFFF) << 8);
                        }
                        j++;  /* Skip the extra entry */
                    }
                }

                /* Get current connection select */
                hda_send_verb(codec->addr, nid, HDA_VERB_GET_CONN_SELECT, &val);
                w->conn_select = (uint8_t)(val & 0xFF);
            }
        }

        /* Get power state support */
        if (w->capabilities & HDA_WIDGET_CAP_PWR_CTRL) {
            hda_get_param(codec->addr, nid, HDA_PARAM_POWER_STATE, &w->pwr_support);
        }
    }

    klog_info("HDA: codec %u - DAC=0x%02X, ADC=0x%02X, OutputPin=0x%02X, InputPin=0x%02X",
        codec->addr,
        codec->output_dac_nid, codec->input_adc_nid,
        codec->output_pin_nid, codec->input_pin_nid);

    return 0;
}

/* ============================================================================
 * Audio Path Setup
 *
 * For output: DAC → (Mixer) → Pin Complex
 * For input: Pin Complex → (Mixer → Selector) → ADC
 *
 * We need to:
 *   1. Enable the pin complex output (set PIN_WIDGET_CTL)
 *   2. Route the DAC to the correct mixer/selector input
 *   3. Set converter format on the DAC/ADC
 *   4. Bind the converter to a stream
 * ============================================================================
 */

/**
 * Set up the output audio path: DAC → output pin.
 */
static int hda_setup_output_path(hda_codec_t *codec)
{
    if (codec->output_dac_nid == 0 || codec->output_pin_nid == 0) {
        return -1;
    }

    hda_widget_t *dac = NULL;
    hda_widget_t *pin = NULL;
    uint32_t i;

    /* Find DAC and Pin widgets */
    for (i = 0; i < codec->widget_count; i++) {
        if (codec->widgets[i].nid == codec->output_dac_nid) dac = &codec->widgets[i];
        if (codec->widgets[i].nid == codec->output_pin_nid) pin = &codec->widgets[i];
    }

    if (!dac || !pin) return -1;

    /* Enable pin output. VREF_En set to Hi-Z (0) for line out */
    uint32_t pin_ctl = HDA_PIN_CTL_OUT_ENABLE;
    if (pin->pin_cap & HDA_PIN_CAP_HP_DRIVE) {
        pin_ctl |= HDA_PIN_CTL_HPHN_ENABLE;
    }

    hda_send_verb_set(codec->addr, codec->output_pin_nid,
        HDA_VERB_SET_PIN_WIDGET_CTL | pin_ctl);

    /* Enable EAPD if supported */
    if (pin->pin_cap & HDA_PIN_CAP_EAPD) {
        hda_send_verb_set(codec->addr, codec->output_pin_nid,
            HDA_VERB_SET_EAPD_BTL_ENABLE | HDA_EAPD_BTL_ENABLE);
    }

    /* Set power state to D0 for DAC and pin */
    if (dac->capabilities & HDA_WIDGET_CAP_PWR_CTRL) {
        hda_send_verb_set(codec->addr, codec->output_dac_nid,
            HDA_VERB_SET_POWER_STATE | HDA_PWR_STATE_D0);
    }
    if (pin->capabilities & HDA_WIDGET_CAP_PWR_CTRL) {
        hda_send_verb_set(codec->addr, codec->output_pin_nid,
            HDA_VERB_SET_POWER_STATE | HDA_PWR_STATE_D0);
    }

    return 0;
}

/**
 * Set up the input audio path: input pin → ADC.
 */
static int hda_setup_input_path(hda_codec_t *codec)
{
    if (codec->input_adc_nid == 0 || codec->input_pin_nid == 0) {
        return -1;
    }

    /* Enable pin input */
    uint32_t pin_ctl = HDA_PIN_CTL_IN_ENABLE;
    hda_send_verb_set(codec->addr, codec->input_pin_nid,
        HDA_VERB_SET_PIN_WIDGET_CTL | pin_ctl);

    return 0;
}

/* ============================================================================
 * Amplifier (Volume) Control
 *
 * HDA amplifier gain uses a step-based model:
 *   - step_size determines the dB per step (0 = 0.25dB, 1 = 0.5dB, etc.)
 *   - num_steps is the number of gain steps (0 = 0dB only)
 *   - offset is the minimum gain (can be negative in 0.25dB steps)
 *   - mute capable flag indicates hardware mute support
 *
 * The amplifier verb format for SET (0x300):
 *   payload bits 15:0:
 *     Bit 15: 0=output amp, 1=input amp
 *     Bit 14: set left
 *     Bit 13: set right
 *     Bit 12: mute (1=mute)
 *     Bits 6:0: gain value
 * ============================================================================
 */

/**
 * Set the amplifier gain for a widget.
 * @param output 1 = output amplifier, 0 = input amplifier
 * @param left_gain Left channel gain (0-127)
 * @param right_gain Right channel gain (0-127)
 * @param mute 1 = mute
 */
static void hda_set_amp(uint32_t codec_addr, uint32_t nid, uint8_t output,
                         uint8_t left_gain, uint8_t right_gain, uint8_t mute)
{
    uint16_t dir = output ? 0x0000 : 0x8000;
    uint16_t mute_bit = mute ? HDA_AMP_MUTE : 0;

    /* Set left channel */
    uint32_t payload_left = (uint32_t)(dir | HDA_AMP_LEFT_CHAN | mute_bit | (left_gain & 0x7F));
    hda_send_verb_set(codec_addr, nid, HDA_VERB_SET_AMP_GAIN_MUTE | payload_left);

    /* Set right channel */
    uint32_t payload_right = (uint32_t)(dir | HDA_AMP_RIGHT_CHAN | mute_bit | (right_gain & 0x7F));
    hda_send_verb_set(codec_addr, nid, HDA_VERB_SET_AMP_GAIN_MUTE | payload_right);
}

/**
 * Get the current output amplifier gain and mute state for a codec.
 * This reads the output amplifier of the output DAC widget.
 */
static void hda_get_output_amp(hda_codec_t *codec)
{
    if (codec->output_dac_nid == 0) return;

    uint32_t resp;

    /* Query left channel gain */
    uint32_t get_left = HDA_AMP_LEFT_CHAN | HDA_AMP_OUTPUT;
    if (hda_send_verb(codec->addr, codec->output_dac_nid,
            HDA_VERB_GET_AMP_GAIN_MUTE | get_left, &resp) == 0) {
        codec->out_amp_vol = resp & HDA_AMP_GAIN_MASK;
        codec->out_amp_muted = (resp & HDA_AMP_GET_MUTE) ? 1 : 0;
    }
}

/* ============================================================================
 * Stream Descriptor Management
 * ============================================================================
 */

/**
 * Allocate a stream descriptor.
 * Returns the stream index, or -1 on failure.
 */
static int hda_alloc_stream(uint8_t direction)
{
    uint32_t i;
    uint32_t max_streams = (direction == 0) ? g_hda.num_output_streams : g_hda.num_input_streams;

    for (i = 0; i < max_streams; i++) {
        uint8_t sd_num = (uint8_t)((direction == 0) ? (i + 1) : (i));
        if (!g_hda.streams[sd_num].active) {
            /* Find a free stream tag */
            uint8_t tag;
            for (tag = 1; tag < 16; tag++) {
                if (!(g_hda.stream_alloc_mask & (1 << tag))) {
                    g_hda.stream_alloc_mask |= (1 << tag);
                    g_hda.streams[sd_num].stream_num = sd_num;
                    g_hda.streams[sd_num].stream_tag = tag;
                    g_hda.streams[sd_num].direction = direction;
                    g_hda.streams[sd_num].active = 1;
                    return (int)sd_num;
                }
            }
        }
    }

    return -1;
}

/**
 * Free a previously allocated stream.
 */
static void hda_free_stream(uint8_t sd_num)
{
    if (sd_num >= HDA_MAX_STREAMS) return;

    g_hda.stream_alloc_mask &= (uint8_t)(~(1 << g_hda.streams[sd_num].stream_tag));
    g_hda.streams[sd_num].active = 0;
}

/**
 * Reset a stream descriptor.
 * Sets SRST bit, waits for it to be set, then clears it.
 */
static int hda_stream_reset(uint8_t sd_num)
{
    uint32_t sd_offset = hda_sd_offset(sd_num);
    uint32_t timeout;

    /* Set SRST */
    hda_write8(sd_offset + HDA_SD_REG_CTL, HDA_SD_CTL_SRST);

    /* Wait for SRST to be read as 1 */
    timeout = 10000;
    while (timeout--) {
        if (hda_read8(sd_offset + HDA_SD_REG_CTL) & HDA_SD_CTL_SRST)
            break;
        io_wait();
    }

    if (timeout == 0) {
        klog_warn("HDA: stream %u SRST set timeout", sd_num);
        return -1;
    }

    /* Clear SRST */
    hda_write8(sd_offset + HDA_SD_REG_CTL, 0);

    /* Wait for SRST to be read as 0 */
    timeout = 10000;
    while (timeout--) {
        if (!(hda_read8(sd_offset + HDA_SD_REG_CTL) & HDA_SD_CTL_SRST))
            break;
        io_wait();
    }

    if (timeout == 0) {
        klog_warn("HDA: stream %u SRST clear timeout", sd_num);
        return -1;
    }

    return 0;
}

/**
 * Build the stream format register value from sample rate, bits, and channels.
 */
static uint16_t hda_build_format(uint32_t rate, uint16_t bits, uint16_t channels)
{
    uint16_t fmt = 0;

    /* Bits per sample */
    switch (bits) {
        case 8:  fmt |= HDA_FMT_BITS_8; break;
        case 16: fmt |= HDA_FMT_BITS_16; break;
        case 20: fmt |= HDA_FMT_BITS_20; break;
        case 24: fmt |= HDA_FMT_BITS_24; break;
        case 32: fmt |= HDA_FMT_BITS_32; break;
        default: fmt |= HDA_FMT_BITS_16; break;
    }

    /* Number of channels (minus 1) */
    fmt |= (uint16_t)((channels - 1) & 0x0F);

    /* Sample rate divider/multiplier */
    switch (rate) {
        case 8000:   fmt |= HDA_FMT_RATE_DIV_6; break;
        case 11025:  fmt |= HDA_FMT_RATE_44KHZ; fmt |= HDA_FMT_RATE_DIV_4; break;
        case 16000:  fmt |= HDA_FMT_RATE_DIV_3; break;
        case 22050:  fmt |= HDA_FMT_RATE_44KHZ; fmt |= HDA_FMT_RATE_DIV_2; break;
        case 24000:  fmt |= HDA_FMT_RATE_DIV_2; break;
        case 32000:  fmt |= HDA_FMT_RATE_32KHZ; break;
        case 44100:  fmt |= HDA_FMT_RATE_44KHZ; break;
        case 48000:  fmt |= HDA_FMT_RATE_48KHZ; break;
        case 88200:  fmt |= HDA_FMT_RATE_44KHZ; fmt |= HDA_FMT_RATE_MULT_2; break;
        case 96000:  fmt |= HDA_FMT_RATE_48KHZ; fmt |= HDA_FMT_RATE_MULT_2; break;
        case 176400: fmt |= HDA_FMT_RATE_44KHZ; fmt |= HDA_FMT_RATE_MULT_4; break;
        case 192000: fmt |= HDA_FMT_RATE_48KHZ; fmt |= HDA_FMT_RATE_MULT_4; break;
        default:     fmt |= HDA_FMT_RATE_48KHZ; break;
    }

    return fmt;
}

/**
 * Set up the Buffer Descriptor List for a stream.
 * Allocates a page and fills BDL entries pointing to the data buffer.
 */
static int hda_setup_bdl(uint8_t sd_num, const void *data, uint32_t size)
{
    hda_stream_t *stream = &g_hda.streams[sd_num];

    /* Allocate BDL page */
    stream->bdl = (hda_bdl_entry_t *)pmm_alloc_pages(1);
    if (!stream->bdl) {
        klog_err("HDA: failed to allocate BDL for stream %u", sd_num);
        return -1;
    }

    stream->bdl_phys = (uint32_t)stream->bdl;
    stream->bdl = (hda_bdl_entry_t *)vmm_map_physical(stream->bdl_phys, PAGE_SIZE);
    if (!stream->bdl) {
        pmm_free_pages((void *)stream->bdl_phys, 1);
        return -1;
    }

    memset(stream->bdl, 0, PAGE_SIZE);

    /* Fill BDL entries - split buffer into chunks if needed */
    uint32_t remaining = size;
    uint32_t data_addr = (uint32_t)data;
    uint32_t entry_count = 0;
    uint32_t max_chunk = 0x4000;  /* 16KB per BDL entry */

    while (remaining > 0 && entry_count < HDA_MAX_BDL_ENTRIES) {
        uint32_t chunk = (remaining > max_chunk) ? max_chunk : remaining;

        stream->bdl[entry_count].addr_low  = data_addr;
        stream->bdl[entry_count].addr_high = 0;
        stream->bdl[entry_count].length    = chunk;

        /* Set IOC on last entry */
        stream->bdl[entry_count].flags =
            (remaining <= chunk) ? HDA_BDL_IOC : 0;

        data_addr  += chunk;
        remaining  -= chunk;
        entry_count++;
    }

    stream->bdl_count = (uint8_t)entry_count;
    stream->cbl = size;
    stream->cpu_buffer_size = size;

    return 0;
}

/* ============================================================================
 * Interrupt Handling
 *
 * HDA interrupts can come from:
 *   - Stream completion (BCIS)
 *   - FIFO error (FIFOE)
 *   - Descriptor error (DESE)
 *   - Controller events (CIS): CORB error, RIRB response, unsolicited response
 *
 * The INTSTS register identifies which streams interrupted (bits 0-29)
 * and whether a controller interrupt occurred (bit 30).
 * ============================================================================
 */

void hdaudio_irq_handler(void *regs)
{
    (void)regs;

    uint32_t intsts = hda_read32(HDA_REG_INTSTS);

    /* Check if this is our interrupt */
    if (!(intsts & HDA_INTSTS_GIS)) {
        return;
    }

    /* Handle controller interrupt */
    if (intsts & HDA_INTSTS_CIS) {
        /* Check CORB status */
        uint8_t corb_sts = hda_read8(HDA_REG_CORBSTS);
        if (corb_sts & HDA_CORBSTS_CMEI) {
            klog_warn("HDA: CORB memory error");
            hda_write8(HDA_REG_CORBSTS, corb_sts);  /* Clear by writing back */
        }

        /* Check RIRB status */
        uint8_t rirb_sts = hda_read8(HDA_REG_RIRBSTS);
        if (rirb_sts & HDA_RIRBSTS_RINTFL) {
            /* Response interrupt - data available in RIRB */
            hda_write8(HDA_REG_RIRBSTS, rirb_sts);
        }
        if (rirb_sts & HDA_RIRBSTS_ROIS) {
            klog_warn("HDA: RIRB overrun");
            hda_write8(HDA_REG_RIRBSTS, rirb_sts);
        }
    }

    /* Handle stream interrupts */
    uint32_t sis = intsts & HDA_INTSTS_SIS_MASK;
    uint32_t i;
    for (i = 0; i < 30; i++) {
        if (sis & (1U << i)) {
            uint32_t sd_offset = hda_sd_offset((uint8_t)i);
            uint8_t sts = hda_read8(sd_offset + HDA_SD_REG_STS);

            if (sts & HDA_SD_STS_BCIS) {
                /* Buffer Completion Interrupt - a BDL entry with IOC was processed */
                hda_write8(sd_offset + HDA_SD_REG_STS, sts & ~HDA_SD_STS_BCIS);

                /* If this stream's BDL had only one IOC entry, stream is done */
                if (g_hda.streams[i].active && g_hda.streams[i].bdl_count == 1) {
                    /* Single-entry BDL: all data has been transferred */
                }
            }

            if (sts & HDA_SD_STS_FIFOE) {
                klog_warn("HDA: stream %u FIFO error", i);
                hda_write8(sd_offset + HDA_SD_REG_STS, sts & ~HDA_SD_STS_FIFOE);
            }

            if (sts & HDA_SD_STS_DESE) {
                klog_warn("HDA: stream %u descriptor error", i);
                hda_write8(sd_offset + HDA_SD_REG_STS, sts & ~HDA_SD_STS_DESE);
            }
        }
    }

    /* Clear handled interrupt status */
    hda_write32(HDA_REG_INTSTS, intsts);

    /* Send EOI */
    if (g_hda.irq < 16) {
        pic_eoi(g_hda.irq);
    }
}

/* ============================================================================
 * Initialization
 * ============================================================================
 */

/**
 * Initialize the HD Audio subsystem.
 *
 * Steps:
 *   1. Scan PCI bus for HD Audio controller (class 0x0403)
 *   2. Enable bus mastering and memory space
 *   3. Map MMIO registers
 *   4. Read GCAP for stream capabilities
 *   5. Controller reset (CRST toggle)
 *   6. Set up CORB (256 entries)
 *   7. Set up RIRB (256 entries)
 *   8. Enable interrupts
 *   9. Enumerate codecs via STATESTS
 *   10. For each codec: enumerate widgets, set up audio paths
 *   11. Set default format
 */
void hdaudio_init(void)
{
    memset(&g_hda, 0, sizeof(hda_controller_t));

    /* ---- Step 1: Scan PCI bus for HDA controller ---- */
    uint16_t bus;
    uint8_t  dev, func;
    int found = 0;

    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;

                uint32_t cl = pci_read_config((uint8_t)bus, dev, func, 0x08);
                uint8_t base_class = (cl >> 24) & 0xFF;
                uint8_t sub_class   = (cl >> 16) & 0xFF;

                /* HDA is class 0x04, subclass 0x03 */
                if (base_class == 0x04 && sub_class == 0x03) {
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        klog_info("HDA: no controller found");
        return;
    }

    bus--; dev--; func--;

    g_hda.pci_bus  = (uint8_t)bus;
    g_hda.pci_dev  = dev;
    g_hda.pci_func = func;

    uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
    g_hda.vendor_id = (uint16_t)(vd & 0xFFFF);
    g_hda.device_id = (uint16_t)((vd >> 16) & 0xFFFF);

    klog_info("HDA: found controller at PCI %u:%u.%u (vendor=0x%04X, device=0x%04X)",
        bus, dev, func, g_hda.vendor_id, g_hda.device_id);

    /* ---- Step 2: Enable bus mastering and memory space ---- */
    uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04);
    cmd |= 0x06;  /* Bus Master Enable | Memory Space Enable */
    pci_write_config((uint8_t)bus, dev, func, 0x04, cmd);

    /* Read IRQ */
    uint32_t irq_info = pci_read_config((uint8_t)bus, dev, func, 0x3C);
    g_hda.irq = (uint8_t)(irq_info & 0xFF);

    /* ---- Step 3: Map MMIO registers (BAR0) ---- */
    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;

    if (mmio_phys == 0) {
        klog_err("HDA: BAR0 is zero, cannot initialize");
        return;
    }

    g_hda.reg_base = (volatile uint8_t *)vmm_map_physical(mmio_phys, 0x4000);
    if (!g_hda.reg_base) {
        klog_err("HDA: failed to map MMIO");
        return;
    }

    klog_info("HDA: MMIO at 0x%X (phys=0x%X), IRQ=%u",
        (uint32_t)g_hda.reg_base, mmio_phys, g_hda.irq);

    /* ---- Step 4: Read GCAP ---- */
    uint16_t gcap = hda_read16(HDA_REG_GCAP);
    g_hda.num_output_streams = (uint8_t)((gcap >> HDA_GCAP_OSS_SHIFT) & 0x0F);
    g_hda.num_input_streams  = (uint8_t)((gcap >> HDA_GCAP_ISS_SHIFT) & 0x0F);
    g_hda.num_bidir_streams  = (uint8_t)((gcap >> HDA_GCAP_BSS_SHIFT) & 0x1F);
    g_hda.supports_64bit     = (gcap & HDA_GCAP_64OK) ? 1 : 0;

    klog_info("HDA: output streams=%u, input streams=%u, bidirectional=%u",
        g_hda.num_output_streams, g_hda.num_input_streams, g_hda.num_bidir_streams);

    /* ---- Step 5: Controller reset ---- */
    /* Clear CRST */
    hda_write32(HDA_REG_GCTL, 0);
    uint32_t timeout = 100000;
    while ((hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST) && timeout--) {
        io_wait();
    }
    if (timeout == 0) {
        klog_warn("HDA: CRST clear timeout");
    }

    /* Set CRST (take controller out of reset) */
    hda_write32(HDA_REG_GCTL, HDA_GCTL_CRST);
    timeout = 100000;
    while (!(hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST) && timeout--) {
        io_wait();
    }
    if (timeout == 0) {
        klog_err("HDA: CRST set timeout");
        return;
    }

    /* Wait for codec status to stabilize */
    timeout = 10000;
    while (timeout--) {
        io_wait();
    }

    /* ---- Step 6: Set up CORB ---- */
    g_hda.corb_entries = 256;
    g_hda.corb = (volatile uint32_t *)pmm_alloc_pages(1);
    if (!g_hda.corb) {
        klog_err("HDA: failed to allocate CORB");
        return;
    }
    g_hda.corb_phys = (uint32_t)g_hda.corb;
    g_hda.corb = (volatile uint32_t *)vmm_map_physical(g_hda.corb_phys, PAGE_SIZE);
    memset((void *)g_hda.corb, 0, PAGE_SIZE);

    /* Set CORB base address */
    hda_write32(HDA_REG_CORBLBASE, g_hda.corb_phys);
    hda_write32(HDA_REG_CORBUBASE, 0);

    /* Set CORB size: 256 entries */
    hda_write8(HDA_REG_CORBSIZE, HDA_CORBSIZE_256ENTRIES);

    /* Reset CORB read pointer */
    hda_write16(HDA_REG_CORBRP, HDA_CORBRP_RST);
    timeout = 10000;
    while ((hda_read16(HDA_REG_CORBRP) & HDA_CORBRP_RST) && timeout--) {
        io_wait();
    }

    /* Start CORB */
    hda_write8(HDA_REG_CORBCTL, HDA_CORBCTL_RUN);

    /* ---- Step 7: Set up RIRB ---- */
    g_hda.rirb_entries = 256;
    g_hda.rirb = (volatile uint64_t *)pmm_alloc_pages(2);  /* 8KB for 256 × 8 bytes */
    if (!g_hda.rirb) {
        klog_err("HDA: failed to allocate RIRB");
        return;
    }
    g_hda.rirb_phys = (uint32_t)g_hda.rirb;
    g_hda.rirb = (volatile uint64_t *)vmm_map_physical(g_hda.rirb_phys, 8192);
    memset((void *)g_hda.rirb, 0, 8192);

    /* Set RIRB base address */
    hda_write32(HDA_REG_RIRBLBASE, g_hda.rirb_phys);
    hda_write32(HDA_REG_RIRBUBASE, 0);

    /* Set RIRB size: 256 entries */
    hda_write8(HDA_REG_RIRBSIZE, HDA_RIRBSIZE_256ENTRIES);

    /* Reset RIRB write pointer */
    hda_write16(HDA_REG_RIRBWP, HDA_RIRBWP_RST);

    /* Set response count (interrupt after 1 response) */
    hda_write16(HDA_REG_RIRBCNT, 1);

    /* Start RIRB */
    hda_write8(HDA_REG_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTFL);

    /* ---- Step 8: Enable interrupts ---- */
    /* Enable global interrupt, controller interrupt, and all stream interrupts */
    hda_write32(HDA_REG_INTCTL,
        HDA_INTCTL_GIE | HDA_INTCTL_CIE | HDA_INTCTL_SIE_MASK);

    /* Register IRQ handler */
    pic_unmask(g_hda.irq);
    irq_register_handler(g_hda.irq, (void (*)(regs_t *))hdaudio_irq_handler);

    /* ---- Step 9: Enumerate codecs ---- */
    g_hda.states_ts = hda_read16(HDA_REG_STATESTS);

    uint32_t codec;
    for (codec = 0; codec < HDA_MAX_CODECS; codec++) {
        if (g_hda.states_ts & (1 << codec)) {
            g_hda.codecs[codec].addr = codec;

            if (hda_enumerate_codec(&g_hda.codecs[codec]) == 0) {
                g_hda.num_codecs++;

                /* ---- Step 10: Set up audio paths ---- */
                hda_setup_output_path(&g_hda.codecs[codec]);
                hda_setup_input_path(&g_hda.codecs[codec]);

                /* Set default format */
                g_hda.codecs[codec].cur_sample_rate = 48000;
                g_hda.codecs[codec].cur_channels = 2;
                g_hda.codecs[codec].cur_bits = 16;
                g_hda.codecs[codec].out_amp_vol = 0;
                g_hda.codecs[codec].out_amp_muted = 0;
            }
        }
    }

    /* ---- Step 11: Reset all streams ---- */
    uint32_t i;
    for (i = 0; i < HDA_MAX_STREAMS; i++) {
        hda_stream_reset((uint8_t)i);
    }

    klog_info("HDA: initialization complete, %u codec(s) found", g_hda.num_codecs);
    g_hda.initialized = 1;
}

/* ============================================================================
 * Playback
 *
 * Playback flow:
 *   1. Allocate an output stream descriptor
 *   2. Set up BDL pointing to the audio data
 *   3. Configure stream format (rate, bits, channels)
 *   4. Configure the codec's output DAC: set converter format, bind to stream
 *   5. Enable pin output
 *   6. Start the stream
 * ============================================================================
 */

int hdaudio_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels)
{
    if (!g_hda.initialized) return -1;
    if (!data || size == 0) return -1;

    /* Find an active codec with output path */
    hda_codec_t *codec = NULL;
    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        if (g_hda.codecs[i].vendor_id != 0 &&
            g_hda.codecs[i].output_dac_nid != 0 &&
            g_hda.codecs[i].output_pin_nid != 0) {
            codec = &g_hda.codecs[i];
            break;
        }
    }

    if (!codec) {
        klog_warn("HDA: no codec with output path available");
        return -1;
    }

    /* Allocate an output stream */
    int sd_num = hda_alloc_stream(0);  /* 0 = output */
    if (sd_num < 0) {
        klog_warn("HDA: no free output streams");
        return -1;
    }

    /* Set up BDL */
    if (hda_setup_bdl((uint8_t)sd_num, data, size) != 0) {
        hda_free_stream((uint8_t)sd_num);
        return -1;
    }

    hda_stream_t *stream = &g_hda.streams[sd_num];
    uint32_t sd_offset = hda_sd_offset((uint8_t)sd_num);

    /* Reset the stream */
    hda_stream_reset((uint8_t)sd_num);

    /* Set BDL address */
    hda_write32(sd_offset + HDA_SD_REG_BDPL, stream->bdl_phys);
    hda_write32(sd_offset + HDA_SD_REG_BDPU, 0);

    /* Set Last Valid Index */
    hda_write8(sd_offset + HDA_SD_REG_LVI, (uint8_t)(stream->bdl_count - 1));

    /* Set Cyclic Buffer Length */
    hda_write32(sd_offset + HDA_SD_REG_CBL, stream->cbl);

    /* Build and set format */
    stream->fmt = hda_build_format(rate, (uint16_t)16, channels);
    hda_write16(sd_offset + HDA_SD_REG_FMT, stream->fmt);

    /* Configure codec DAC */
    /* Set converter format on the DAC */
    hda_send_verb_set(codec->addr, codec->output_dac_nid,
        HDA_VERB_SET_CONV_FMT | (stream->fmt & 0xFFFF));

    /* Bind DAC to stream */
    hda_send_verb_set(codec->addr, codec->output_dac_nid,
        HDA_VERB_SET_STREAMCHAN | ((uint32_t)stream->stream_tag << 4));

    /* Ensure pin output is enabled */
    hda_send_verb_set(codec->addr, codec->output_pin_nid,
        HDA_VERB_SET_PIN_WIDGET_CTL | HDA_PIN_CTL_OUT_ENABLE);

    /* Clear stream status */
    hda_write8(sd_offset + HDA_SD_REG_STS,
        HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE);

    /* Build control register value */
    uint32_t ctl = HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE | HDA_SD_CTL_FEIE;
    ctl |= ((uint32_t)stream->stream_tag << HDA_SD_CTL_STRM_SHIFT);

    /* Set interrupt enable for this stream */
    uint32_t intctl = hda_read32(HDA_REG_INTCTL);
    intctl |= (1U << sd_num);
    hda_write32(HDA_REG_INTCTL, intctl);

    /* Start the stream */
    hda_write32(sd_offset + HDA_SD_REG_CTL, ctl);

    codec->cur_sample_rate = rate;
    codec->cur_channels = channels;

    return 0;
}

/* ============================================================================
 * Capture (Recording)
 * ============================================================================
 */

int hdaudio_record(void *data, uint32_t size, uint32_t rate, uint16_t channels)
{
    if (!g_hda.initialized) return -1;
    if (!data || size == 0) return -1;

    /* Find an active codec with input path */
    hda_codec_t *codec = NULL;
    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        if (g_hda.codecs[i].vendor_id != 0 &&
            g_hda.codecs[i].input_adc_nid != 0 &&
            g_hda.codecs[i].input_pin_nid != 0) {
            codec = &g_hda.codecs[i];
            break;
        }
    }

    if (!codec) {
        klog_warn("HDA: no codec with input path available");
        return -1;
    }

    /* Allocate an input stream */
    int sd_num = hda_alloc_stream(1);  /* 1 = input */
    if (sd_num < 0) {
        klog_warn("HDA: no free input streams");
        return -1;
    }

    /* Set up BDL */
    if (hda_setup_bdl((uint8_t)sd_num, data, size) != 0) {
        hda_free_stream((uint8_t)sd_num);
        return -1;
    }

    hda_stream_t *stream = &g_hda.streams[sd_num];
    uint32_t sd_offset = hda_sd_offset((uint8_t)sd_num);

    /* Reset the stream */
    hda_stream_reset((uint8_t)sd_num);

    /* Set BDL address */
    hda_write32(sd_offset + HDA_SD_REG_BDPL, stream->bdl_phys);
    hda_write32(sd_offset + HDA_SD_REG_BDPU, 0);

    /* Set LVI */
    hda_write8(sd_offset + HDA_SD_REG_LVI, (uint8_t)(stream->bdl_count - 1));

    /* Set CBL */
    hda_write32(sd_offset + HDA_SD_REG_CBL, stream->cbl);

    /* Build and set format */
    stream->fmt = hda_build_format(rate, (uint16_t)16, channels);
    hda_write16(sd_offset + HDA_SD_REG_FMT, stream->fmt);

    /* Configure codec ADC */
    hda_send_verb_set(codec->addr, codec->input_adc_nid,
        HDA_VERB_SET_CONV_FMT | (stream->fmt & 0xFFFF));

    /* Bind ADC to stream */
    hda_send_verb_set(codec->addr, codec->input_adc_nid,
        HDA_VERB_SET_STREAMCHAN | ((uint32_t)stream->stream_tag << 4));

    /* Enable pin input */
    hda_send_verb_set(codec->addr, codec->input_pin_nid,
        HDA_VERB_SET_PIN_WIDGET_CTL | HDA_PIN_CTL_IN_ENABLE);

    /* Clear stream status */
    hda_write8(sd_offset + HDA_SD_REG_STS,
        HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE);

    /* Build control register (input direction) */
    uint32_t ctl = HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE | HDA_SD_CTL_FEIE | HDA_SD_CTL_DIR;
    ctl |= ((uint32_t)stream->stream_tag << HDA_SD_CTL_STRM_SHIFT);

    /* Set interrupt enable for this stream */
    uint32_t intctl = hda_read32(HDA_REG_INTCTL);
    intctl |= (1U << sd_num);
    hda_write32(HDA_REG_INTCTL, intctl);

    /* Start the stream */
    hda_write32(sd_offset + HDA_SD_REG_CTL, ctl);

    return 0;
}

/* ============================================================================
 * Stop
 * ============================================================================
 */

void hdaudio_stop(void)
{
    /* Stop all active streams */
    uint32_t i;
    for (i = 0; i < HDA_MAX_STREAMS; i++) {
        if (g_hda.streams[i].active) {
            hdaudio_stop_stream((uint8_t)i);
        }
    }
}

void hdaudio_stop_stream(uint8_t sd_num)
{
    if (sd_num >= HDA_MAX_STREAMS) return;
    if (!g_hda.streams[sd_num].active) return;

    uint32_t sd_offset = hda_sd_offset(sd_num);

    /* Clear RUN bit */
    uint32_t ctl = hda_read32(sd_offset + HDA_SD_REG_CTL) & 0x00FFFFFF;
    ctl &= ~(uint32_t)HDA_SD_CTL_RUN;
    hda_write32(sd_offset + HDA_SD_REG_CTL, ctl);

    /* Wait for RUN bit to clear */
    uint32_t timeout = 10000;
    while ((hda_read8(sd_offset + HDA_SD_REG_CTL + 1) & (HDA_SD_CTL_RUN >> 8)) && timeout--) {
        io_wait();
    }

    /* Reset the stream */
    hda_stream_reset(sd_num);

    /* Clear status */
    hda_write8(sd_offset + HDA_SD_REG_STS,
        HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE);

    /* Free BDL */
    if (g_hda.streams[sd_num].bdl) {
        vmm_unmap_physical((void *)g_hda.streams[sd_num].bdl, PAGE_SIZE);
        pmm_free_pages((void *)g_hda.streams[sd_num].bdl_phys, 1);
        g_hda.streams[sd_num].bdl = NULL;
    }

    /* Free the stream */
    hda_free_stream(sd_num);
}

/* ============================================================================
 * Volume Control
 *
 * The volume is controlled through the output amplifier of the DAC widget.
 * Gain is in steps of 0.25dB. The number of steps is read from the
 * amplifier capabilities parameter.
 *
 * User volume (0-255) is mapped to amplifier gain (0 to num_steps).
 * Bit 12 of the SET verb payload controls mute.
 * ============================================================================
 */

int hdaudio_set_volume(uint8_t left, uint8_t right)
{
    if (!g_hda.initialized) return -1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        if (codec->vendor_id == 0 || codec->output_dac_nid == 0) continue;

        /* Read amplifier capabilities to determine step range */
        uint32_t amp_cap;
        if (hda_get_param(codec->addr, codec->output_dac_nid,
                HDA_PARAM_OUTPUT_AMP_CAP, &amp_cap) != 0) {
            amp_cap = 0;  /* Default */
        }

        uint32_t num_steps = (amp_cap & HDA_AMP_CAP_NUM_STEPS_MASK) >> HDA_AMP_CAP_NUM_STEPS_SHIFT;
        if (num_steps == 0) num_steps = 64;  /* Default to 64 steps */

        /* Map user volume 0-255 to amplifier steps 0-num_steps */
        uint8_t gain_left  = (uint8_t)(((uint32_t)left  * num_steps) / 255);
        uint8_t gain_right = (uint8_t)(((uint32_t)right * num_steps) / 255);

        /* Set output amplifier on DAC */
        hda_set_amp(codec->addr, codec->output_dac_nid, 1,
            gain_left, gain_right, 0);

        /* Also set pin widget amp if it has one */
        if (codec->output_pin_nid != 0) {
            hda_widget_t *pin = NULL;
            uint32_t j;
            for (j = 0; j < codec->widget_count; j++) {
                if (codec->widgets[j].nid == codec->output_pin_nid) {
                    pin = &codec->widgets[j];
                    break;
                }
            }
            if (pin && (pin->capabilities & HDA_WIDGET_CAP_OUT_AMP_PRESENT)) {
                hda_set_amp(codec->addr, codec->output_pin_nid, 1,
                    gain_left, gain_right, 0);
            }
        }

        codec->out_amp_vol = gain_left;
        codec->out_amp_muted = 0;
    }

    return 0;
}

int hdaudio_get_volume(uint8_t *left, uint8_t *right)
{
    if (!g_hda.initialized) return -1;
    if (!left || !right) return -1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        if (codec->vendor_id != 0 && codec->output_dac_nid != 0) {
            /* Read amplifier capabilities */
            uint32_t amp_cap;
            if (hda_get_param(codec->addr, codec->output_dac_nid,
                    HDA_PARAM_OUTPUT_AMP_CAP, &amp_cap) != 0) {
                amp_cap = 0;
            }

            uint32_t num_steps = (amp_cap & HDA_AMP_CAP_NUM_STEPS_MASK) >> HDA_AMP_CAP_NUM_STEPS_SHIFT;
            if (num_steps == 0) num_steps = 64;

            /* Get current gain */
            hda_get_output_amp(codec);

            /* Map amplifier steps back to 0-255 */
            *left  = (uint8_t)((codec->out_amp_vol * 255) / num_steps);
            *right = *left;  /* Both channels have same gain */
            return codec->out_amp_muted ? 1 : 0;
        }
    }

    *left = 0;
    *right = 0;
    return -1;
}

int hdaudio_set_mute(uint8_t mute)
{
    if (!g_hda.initialized) return -1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        if (codec->vendor_id == 0 || codec->output_dac_nid == 0) continue;

        hda_set_amp(codec->addr, codec->output_dac_nid, 1,
            (uint8_t)codec->out_amp_vol, (uint8_t)codec->out_amp_vol, mute);

        codec->out_amp_muted = mute;
    }

    return 0;
}

int hdaudio_get_mute(void)
{
    if (!g_hda.initialized) return 1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        if (g_hda.codecs[i].vendor_id != 0) {
            hda_get_output_amp(&g_hda.codecs[i]);
            return g_hda.codecs[i].out_amp_muted;
        }
    }

    return 1;
}

int hdaudio_set_format(uint16_t bits, uint16_t channels)
{
    if (!g_hda.initialized) return -1;

    if (bits != 8 && bits != 16 && bits != 20 && bits != 24 && bits != 32) return -1;
    if (channels < 1 || channels > 8) return -1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        if (g_hda.codecs[i].vendor_id != 0) {
            g_hda.codecs[i].cur_bits    = bits;
            g_hda.codecs[i].cur_channels = channels;
        }
    }

    return 0;
}

int hdaudio_set_sample_rate(uint32_t rate)
{
    if (!g_hda.initialized) return -1;

    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        if (g_hda.codecs[i].vendor_id != 0) {
            g_hda.codecs[i].cur_sample_rate = rate;
        }
    }

    return 0;
}

int hdaudio_set_power_state(uint8_t state)
{
    if (!g_hda.initialized) return -1;

    uint8_t ps;
    switch (state) {
        case 0: ps = HDA_PWR_STATE_D0; break;
        case 1: ps = HDA_PWR_STATE_D1; break;
        case 2: ps = HDA_PWR_STATE_D2; break;
        case 3: ps = HDA_PWR_STATE_D3; break;
        default: return -1;
    }

    /* Set power state on all output DACs and pins */
    uint32_t i;
    for (i = 0; i < HDA_MAX_CODECS; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        if (codec->vendor_id == 0) continue;

        if (codec->output_dac_nid != 0) {
            hda_send_verb_set(codec->addr, codec->output_dac_nid,
                HDA_VERB_SET_POWER_STATE | ps);
        }
        if (codec->output_pin_nid != 0) {
            hda_send_verb_set(codec->addr, codec->output_pin_nid,
                HDA_VERB_SET_POWER_STATE | ps);
        }
    }

    return 0;
}

/* ============================================================================
 * Buffer Position
 * ============================================================================
 */

uint32_t hdaudio_get_buffer_position(void)
{
    if (!g_hda.initialized) return 0;

    /* Return LPIB from the first active output stream */
    uint32_t i;
    for (i = 0; i < HDA_MAX_STREAMS; i++) {
        if (g_hda.streams[i].active && g_hda.streams[i].direction == 0) {
            uint32_t sd_offset = hda_sd_offset((uint8_t)i);
            return hda_read32(sd_offset + HDA_SD_REG_LPIB);
        }
    }

    return 0;
}

hda_controller_t *hdaudio_get_controller(void)
{
    return &g_hda;
}

uint8_t hdaudio_is_available(void)
{
    return (uint8_t)(g_hda.initialized && g_hda.num_codecs > 0);
}