// Settings lib
void revk_settings_load (const char *tag,const char *appname);
const char * revk_setting_dump (int level);
void revk_settings_commit(void);
revk_settings_t *revk_settings_find(const char *name,int *index);
char * revk_settings_text(revk_settings_t * s, int index, int *lenp);

