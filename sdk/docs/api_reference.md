# FUNSOS SDK API Reference

## Core Functions

### System
- `uint32_t funsos_get_pid(void)` - Get current process ID
- `const char *funsos_get_version(void)` - Get OS version string
- `int funsos_get_memory_info(uint32_t *total, uint32_t *used)` - Get memory stats
- `void funsos_exit(int status)` - Exit current process
- `void funsos_yield(void)` - Yield CPU to other processes
- `int funsos_sleep(uint32_t seconds)` - Sleep for seconds

### Window Management
- `funsos_window_t funsos_create_window(int x, int y, int w, int h, const char *title)` - Create window
- `int funsos_destroy_window(funsos_window_t win)` - Destroy window
- `void funsos_show_window(funsos_window_t win)` - Show window
- `void funsos_hide_window(funsos_window_t win)` - Hide window
- `void funsos_move_window(funsos_window_t win, int x, int y)` - Move window
- `void funsos_resize_window(funsos_window_t win, int w, int h)` - Resize window

### 2D Graphics
- `int funsos_draw_rect(win, x, y, w, h, color)` - Draw rectangle
- `int funsos_draw_text(win, x, y, text, fg)` - Draw text
- `int funsos_draw_line(win, x1, y1, x2, y2, color)` - Draw line
- `int funsos_fill_window(win, bg)` - Fill window with color
- `void funsos_draw_circle(ctx, cx, cy, r, color)` - Draw circle
- `void funsos_fill_circle(ctx, cx, cy, r, color)` - Fill circle
- `void funsos_fill_rounded_rect(ctx, rect, radius, color)` - Fill rounded rect

### 3D Graphics
- `void funsos_3d_init(ctx)` - Initialize 3D context
- `funsos_mat4_t funsos_3d_perspective(fov, aspect, near, far)` - Perspective matrix
- `funsos_mat4_t funsos_3d_lookat(eye, center, up)` - View matrix
- `funsos_mat4_t funsos_3d_rotate_y(angle)` - Y rotation matrix
- `void funsos_3d_render(vertices, count, mvp, mode)` - Render 3D vertices

### Events
- `int funsos_poll_event(event)` - Poll for event (non-blocking)
- `int funsos_wait_event(event)` - Wait for event (blocking)
- `int funsos_set_timer(interval_ms)` - Set timer event

### File System
- `int funsos_file_open(path, mode)` - Open file
- `int funsos_file_read(fd, buf, count)` - Read from file
- `int funsos_file_write(fd, buf, count)` - Write to file
- `int funsos_file_close(fd)` - Close file
- `int funsos_file_mkdir(path)` - Create directory
- `int funsos_file_chdir(path)` - Change directory

### Process Management
- `int funsos_fork(void)` - Fork process
- `int funsos_exec(path, argv)` - Execute program
- `int funsos_spawn(path, args)` - Spawn process
- `int funsos_wait(status)` - Wait for child
- `int funsos_signal(sig, handler)` - Set signal handler
- `int funsos_kill(pid, sig)` - Send signal

### Networking
- `int funsos_socket(domain, type, protocol)` - Create socket
- `int funsos_connect(fd, addr)` - Connect to server
- `int funsos_send(fd, buf, len, flags)` - Send data
- `int funsos_recv(fd, buf, len, flags)` - Receive data
- `int funsos_closesocket(fd)` - Close socket

### Audio
- `int funsos_audio_init(void)` - Initialize audio
- `int funsos_audio_play(dev, data, size)` - Play audio data
- `int funsos_audio_play_wav(path)` - Play WAV file
- `int funsos_audio_set_volume(dev, left, right)` - Set volume

### Threading
- `int funsos_pthread_create(thread, attr, func, arg)` - Create thread
- `int funsos_pthread_join(thread, retval)` - Join thread
- `int funsos_pthread_mutex_init(mutex, attr)` - Init mutex
- `int funsos_pthread_mutex_lock(mutex)` - Lock mutex
- `int funsos_pthread_mutex_unlock(mutex)` - Unlock mutex

### Database (FunDB)
- `fundb_handle_t fundb_open(path)` - Open database
- `int fundb_close(db)` - Close database
- `int fundb_create_table(db, name, columns, count)` - Create table
- `int fundb_insert(db, table, row)` - Insert row
- `fundb_result_t *fundb_select(db, table, cols, where, order, limit)` - Select data
- `fundb_result_t *fundb_query(db, sql)` - Execute SQL
- `void fundb_free_result(result)` - Free result
