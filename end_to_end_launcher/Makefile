CC=gcc
launcher: launcher.c
	$(CC) -o $@ $^ -lnuma -lipmctl
	sudo setcap "all=ep" $@