[BITS 32]

[SECTION .text]

[EXTERN _exception_handler]
[EXTERN _irq_handler]

%MACRO ISR_NOERR 1
isr_stub_%+%1:
    PUSH DWORD 0
    PUSH DWORD %1
    JMP isr_common_stub
%ENDMACRO

%MACRO ISR_ERR 1
isr_stub_%+%1:
    PUSH DWORD %1
    JMP isr_common_stub
%ENDMACRO

%MACRO IRQ_STUB 2
isr_stub_%+%1:
    PUSH DWORD 0
    PUSH DWORD %2
    JMP irq_common_stub
%ENDMACRO

%MACRO SYSCALL_STUB 1
isr_stub_%+%1:
    PUSH DWORD 0
    PUSH DWORD %1
    JMP isr_common_stub
%ENDMACRO

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

IRQ_STUB 32, 0
IRQ_STUB 33, 1
IRQ_STUB 34, 2
IRQ_STUB 35, 3
IRQ_STUB 36, 4
IRQ_STUB 37, 5
IRQ_STUB 38, 6
IRQ_STUB 39, 7
IRQ_STUB 40, 8
IRQ_STUB 41, 9
IRQ_STUB 42, 10
IRQ_STUB 43, 11
IRQ_STUB 44, 12
IRQ_STUB 45, 13
IRQ_STUB 46, 14
IRQ_STUB 47, 15

ISR_NOERR 48
ISR_NOERR 49
ISR_NOERR 50
ISR_NOERR 51
ISR_NOERR 52
ISR_NOERR 53
ISR_NOERR 54
ISR_NOERR 55
ISR_NOERR 56
ISR_NOERR 57
ISR_NOERR 58
ISR_NOERR 59
ISR_NOERR 60
ISR_NOERR 61
ISR_NOERR 62
ISR_NOERR 63
ISR_NOERR 64
ISR_NOERR 65
ISR_NOERR 66
ISR_NOERR 67
ISR_NOERR 68
ISR_NOERR 69
ISR_NOERR 70
ISR_NOERR 71
ISR_NOERR 72
ISR_NOERR 73
ISR_NOERR 74
ISR_NOERR 75
ISR_NOERR 76
ISR_NOERR 77
ISR_NOERR 78
ISR_NOERR 79
ISR_NOERR 80
ISR_NOERR 81
ISR_NOERR 82
ISR_NOERR 83
ISR_NOERR 84
ISR_NOERR 85
ISR_NOERR 86
ISR_NOERR 87
ISR_NOERR 88
ISR_NOERR 89
ISR_NOERR 90
ISR_NOERR 91
ISR_NOERR 92
ISR_NOERR 93
ISR_NOERR 94
ISR_NOERR 95
ISR_NOERR 96
ISR_NOERR 97
ISR_NOERR 98
ISR_NOERR 99
ISR_NOERR 100
ISR_NOERR 101
ISR_NOERR 102
ISR_NOERR 103
ISR_NOERR 104
ISR_NOERR 105
ISR_NOERR 106
ISR_NOERR 107
ISR_NOERR 108
ISR_NOERR 109
ISR_NOERR 110
ISR_NOERR 111
ISR_NOERR 112
ISR_NOERR 113
ISR_NOERR 114
ISR_NOERR 115
ISR_NOERR 116
ISR_NOERR 117
ISR_NOERR 118
ISR_NOERR 119
ISR_NOERR 120
ISR_NOERR 121
ISR_NOERR 122
ISR_NOERR 123
ISR_NOERR 124
ISR_NOERR 125
ISR_NOERR 126
ISR_NOERR 127

isr_stub_128:
    PUSH DWORD 0
    PUSH DWORD 0x80
    JMP isr_common_stub

ISR_NOERR 129
ISR_NOERR 130
ISR_NOERR 131
ISR_NOERR 132
ISR_NOERR 133
ISR_NOERR 134
ISR_NOERR 135
ISR_NOERR 136
ISR_NOERR 137
ISR_NOERR 138
ISR_NOERR 139
ISR_NOERR 140
ISR_NOERR 141
ISR_NOERR 142
ISR_NOERR 143
ISR_NOERR 144
ISR_NOERR 145
ISR_NOERR 146
ISR_NOERR 147
ISR_NOERR 148
ISR_NOERR 149
ISR_NOERR 150
ISR_NOERR 151
ISR_NOERR 152
ISR_NOERR 153
ISR_NOERR 154
ISR_NOERR 155
ISR_NOERR 156
ISR_NOERR 157
ISR_NOERR 158
ISR_NOERR 159
ISR_NOERR 160
ISR_NOERR 161
ISR_NOERR 162
ISR_NOERR 163
ISR_NOERR 164
ISR_NOERR 165
ISR_NOERR 166
ISR_NOERR 167
ISR_NOERR 168
ISR_NOERR 169
ISR_NOERR 170
ISR_NOERR 171
ISR_NOERR 172
ISR_NOERR 173
ISR_NOERR 174
ISR_NOERR 175
ISR_NOERR 176
ISR_NOERR 177
ISR_NOERR 178
ISR_NOERR 179
ISR_NOERR 180
ISR_NOERR 181
ISR_NOERR 182
ISR_NOERR 183
ISR_NOERR 184
ISR_NOERR 185
ISR_NOERR 186
ISR_NOERR 187
ISR_NOERR 188
ISR_NOERR 189
ISR_NOERR 190
ISR_NOERR 191
ISR_NOERR 192
ISR_NOERR 193
ISR_NOERR 194
ISR_NOERR 195
ISR_NOERR 196
ISR_NOERR 197
ISR_NOERR 198
ISR_NOERR 199
ISR_NOERR 200
ISR_NOERR 201
ISR_NOERR 202
ISR_NOERR 203
ISR_NOERR 204
ISR_NOERR 205
ISR_NOERR 206
ISR_NOERR 207
ISR_NOERR 208
ISR_NOERR 209
ISR_NOERR 210
ISR_NOERR 211
ISR_NOERR 212
ISR_NOERR 213
ISR_NOERR 214
ISR_NOERR 215
ISR_NOERR 216
ISR_NOERR 217
ISR_NOERR 218
ISR_NOERR 219
ISR_NOERR 220
ISR_NOERR 221
ISR_NOERR 222
ISR_NOERR 223
ISR_NOERR 224
ISR_NOERR 225
ISR_NOERR 226
ISR_NOERR 227
ISR_NOERR 228
ISR_NOERR 229
ISR_NOERR 230
ISR_NOERR 231
ISR_NOERR 232
ISR_NOERR 233
ISR_NOERR 234
ISR_NOERR 235
ISR_NOERR 236
ISR_NOERR 237
ISR_NOERR 238
ISR_NOERR 239
ISR_NOERR 240
ISR_NOERR 241
ISR_NOERR 242
ISR_NOERR 243
ISR_NOERR 244
ISR_NOERR 245
ISR_NOERR 246
ISR_NOERR 247
ISR_NOERR 248
ISR_NOERR 249
ISR_NOERR 250
ISR_NOERR 251
ISR_NOERR 252
ISR_NOERR 253
ISR_NOERR 254
ISR_NOERR 255

isr_common_stub:
    PUSHAD
    PUSH DS
    PUSH ES
    PUSH FS
    PUSH GS

    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX

    PUSH ESP
    CALL _exception_handler
    ADD ESP, 4

    POP GS
    POP FS
    POP ES
    POP DS
    POPAD
    ADD ESP, 8
    IRET

irq_common_stub:
    PUSHAD
    PUSH DS
    PUSH ES
    PUSH FS
    PUSH GS

    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX

    PUSH ESP
    CALL _irq_handler
    ADD ESP, 4

    ; EOI is handled by C irq_handler (pic_eoi), not here.
    ; Double EOI causes spurious interrupts and lost interrupts.

    POP GS
    POP FS
    POP ES
    POP DS
    POPAD
    ADD ESP, 8
    IRET

[SECTION .data]

[GLOBAL _interrupt_entry_table]

_interrupt_entry_table:
    DD isr_stub_0
    DD isr_stub_1
    DD isr_stub_2
    DD isr_stub_3
    DD isr_stub_4
    DD isr_stub_5
    DD isr_stub_6
    DD isr_stub_7
    DD isr_stub_8
    DD isr_stub_9
    DD isr_stub_10
    DD isr_stub_11
    DD isr_stub_12
    DD isr_stub_13
    DD isr_stub_14
    DD isr_stub_15
    DD isr_stub_16
    DD isr_stub_17
    DD isr_stub_18
    DD isr_stub_19
    DD isr_stub_20
    DD isr_stub_21
    DD isr_stub_22
    DD isr_stub_23
    DD isr_stub_24
    DD isr_stub_25
    DD isr_stub_26
    DD isr_stub_27
    DD isr_stub_28
    DD isr_stub_29
    DD isr_stub_30
    DD isr_stub_31
    DD isr_stub_32
    DD isr_stub_33
    DD isr_stub_34
    DD isr_stub_35
    DD isr_stub_36
    DD isr_stub_37
    DD isr_stub_38
    DD isr_stub_39
    DD isr_stub_40
    DD isr_stub_41
    DD isr_stub_42
    DD isr_stub_43
    DD isr_stub_44
    DD isr_stub_45
    DD isr_stub_46
    DD isr_stub_47
    DD isr_stub_48
    DD isr_stub_49
    DD isr_stub_50
    DD isr_stub_51
    DD isr_stub_52
    DD isr_stub_53
    DD isr_stub_54
    DD isr_stub_55
    DD isr_stub_56
    DD isr_stub_57
    DD isr_stub_58
    DD isr_stub_59
    DD isr_stub_60
    DD isr_stub_61
    DD isr_stub_62
    DD isr_stub_63
    DD isr_stub_64
    DD isr_stub_65
    DD isr_stub_66
    DD isr_stub_67
    DD isr_stub_68
    DD isr_stub_69
    DD isr_stub_70
    DD isr_stub_71
    DD isr_stub_72
    DD isr_stub_73
    DD isr_stub_74
    DD isr_stub_75
    DD isr_stub_76
    DD isr_stub_77
    DD isr_stub_78
    DD isr_stub_79
    DD isr_stub_80
    DD isr_stub_81
    DD isr_stub_82
    DD isr_stub_83
    DD isr_stub_84
    DD isr_stub_85
    DD isr_stub_86
    DD isr_stub_87
    DD isr_stub_88
    DD isr_stub_89
    DD isr_stub_90
    DD isr_stub_91
    DD isr_stub_92
    DD isr_stub_93
    DD isr_stub_94
    DD isr_stub_95
    DD isr_stub_96
    DD isr_stub_97
    DD isr_stub_98
    DD isr_stub_99
    DD isr_stub_100
    DD isr_stub_101
    DD isr_stub_102
    DD isr_stub_103
    DD isr_stub_104
    DD isr_stub_105
    DD isr_stub_106
    DD isr_stub_107
    DD isr_stub_108
    DD isr_stub_109
    DD isr_stub_110
    DD isr_stub_111
    DD isr_stub_112
    DD isr_stub_113
    DD isr_stub_114
    DD isr_stub_115
    DD isr_stub_116
    DD isr_stub_117
    DD isr_stub_118
    DD isr_stub_119
    DD isr_stub_120
    DD isr_stub_121
    DD isr_stub_122
    DD isr_stub_123
    DD isr_stub_124
    DD isr_stub_125
    DD isr_stub_126
    DD isr_stub_127
    DD isr_stub_128
    DD isr_stub_129
    DD isr_stub_130
    DD isr_stub_131
    DD isr_stub_132
    DD isr_stub_133
    DD isr_stub_134
    DD isr_stub_135
    DD isr_stub_136
    DD isr_stub_137
    DD isr_stub_138
    DD isr_stub_139
    DD isr_stub_140
    DD isr_stub_141
    DD isr_stub_142
    DD isr_stub_143
    DD isr_stub_144
    DD isr_stub_145
    DD isr_stub_146
    DD isr_stub_147
    DD isr_stub_148
    DD isr_stub_149
    DD isr_stub_150
    DD isr_stub_151
    DD isr_stub_152
    DD isr_stub_153
    DD isr_stub_154
    DD isr_stub_155
    DD isr_stub_156
    DD isr_stub_157
    DD isr_stub_158
    DD isr_stub_159
    DD isr_stub_160
    DD isr_stub_161
    DD isr_stub_162
    DD isr_stub_163
    DD isr_stub_164
    DD isr_stub_165
    DD isr_stub_166
    DD isr_stub_167
    DD isr_stub_168
    DD isr_stub_169
    DD isr_stub_170
    DD isr_stub_171
    DD isr_stub_172
    DD isr_stub_173
    DD isr_stub_174
    DD isr_stub_175
    DD isr_stub_176
    DD isr_stub_177
    DD isr_stub_178
    DD isr_stub_179
    DD isr_stub_180
    DD isr_stub_181
    DD isr_stub_182
    DD isr_stub_183
    DD isr_stub_184
    DD isr_stub_185
    DD isr_stub_186
    DD isr_stub_187
    DD isr_stub_188
    DD isr_stub_189
    DD isr_stub_190
    DD isr_stub_191
    DD isr_stub_192
    DD isr_stub_193
    DD isr_stub_194
    DD isr_stub_195
    DD isr_stub_196
    DD isr_stub_197
    DD isr_stub_198
    DD isr_stub_199
    DD isr_stub_200
    DD isr_stub_201
    DD isr_stub_202
    DD isr_stub_203
    DD isr_stub_204
    DD isr_stub_205
    DD isr_stub_206
    DD isr_stub_207
    DD isr_stub_208
    DD isr_stub_209
    DD isr_stub_210
    DD isr_stub_211
    DD isr_stub_212
    DD isr_stub_213
    DD isr_stub_214
    DD isr_stub_215
    DD isr_stub_216
    DD isr_stub_217
    DD isr_stub_218
    DD isr_stub_219
    DD isr_stub_220
    DD isr_stub_221
    DD isr_stub_222
    DD isr_stub_223
    DD isr_stub_224
    DD isr_stub_225
    DD isr_stub_226
    DD isr_stub_227
    DD isr_stub_228
    DD isr_stub_229
    DD isr_stub_230
    DD isr_stub_231
    DD isr_stub_232
    DD isr_stub_233
    DD isr_stub_234
    DD isr_stub_235
    DD isr_stub_236
    DD isr_stub_237
    DD isr_stub_238
    DD isr_stub_239
    DD isr_stub_240
    DD isr_stub_241
    DD isr_stub_242
    DD isr_stub_243
    DD isr_stub_244
    DD isr_stub_245
    DD isr_stub_246
    DD isr_stub_247
    DD isr_stub_248
    DD isr_stub_249
    DD isr_stub_250
    DD isr_stub_251
    DD isr_stub_252
    DD isr_stub_253
    DD isr_stub_254
    DD isr_stub_255
