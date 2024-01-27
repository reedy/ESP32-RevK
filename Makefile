revk_settings: revk_settings.c
	gcc -O -o $@ $< -g -Wall --std=gnu99 -lpopt
