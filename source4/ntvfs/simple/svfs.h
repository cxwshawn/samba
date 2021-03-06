
struct svfs_private {
	struct ntvfs_module_context *ntvfs;

	/* the base directory */
	char *connectpath;

	/* a linked list of open searches */
	struct search_state *search;

	/* next available search handle */
	uint16_t next_search_handle;

	struct svfs_file *open_files;
};

struct svfs_dir {
	unsigned int count;
	char *unix_dir;
	struct svfs_dirfile {
		char *name;
		struct stat st;
	} *files;
};

struct svfs_file {
	struct svfs_file *next, *prev;
	int fd;
	struct ntvfs_handle *handle;
	char *name;
};

struct search_state {
	struct search_state *next, *prev;
	uint16_t handle;
	unsigned int current_index;
	struct svfs_dir *dir;
};
