#ifndef ACP_LOOSE_PATHS_H
#define ACP_LOOSE_PATHS_H
/* Snapshot only confirmed ENOENT roots before game threads start. Any attempt
 * to create/write within a root permanently disables its shortcut this run. */
typedef struct {unsigned absent;} acp_loose_paths;
void acp_loose_paths_init(acp_loose_paths *,int (*missing)(const char *));
int acp_loose_paths_missing(const acp_loose_paths *,const char *);
void acp_loose_paths_write(acp_loose_paths *,const char *);
#endif
