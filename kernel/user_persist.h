#ifndef USER_PERSIST_H
#define USER_PERSIST_H

void user_persist_init(void);
int  user_persist_save(void);
int  user_persist_load(void);
int  user_persist_save_groups(void);
int  user_persist_load_groups(void);
int  user_persist_save_env(void);
int  user_persist_load_env(void);

#endif
