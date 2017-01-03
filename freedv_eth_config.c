/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */
#include "freedv_eth_config.h"

#include <stdio.h>
#include <string.h>

struct freedv_eth_config {
	struct freedv_eth_config *next;
	
	char *key;
	char *value;
};

static struct freedv_eth_config *config_list = NULL;

int freedv_eth_config_load(char *file)
{
	FILE *fd;
	char *rf;
	
	fd = fopen(file, "r");
	if (!fd)
		goto err_fopen;
	
	do {
		char buffer[1025];
		char *key, *value;
		
		rf = fgets(buffer, 1024, fd);
		while (strlen(buffer) && buffer[strlen(buffer)-1] == '\n')
			buffer[strlen(buffer)-1] = 0;
		key = strtok(buffer, " \t=");
		value = strtok(NULL, "\n\r");
		if (key && value) {
			struct freedv_eth_config *conf;
			while (value[0] == ' ' ||
			    value[0] == '\t' ||
			    value[0] == '=')
				value++;
			
			conf = calloc(1, sizeof(struct freedv_eth_config));
			if (!conf)
				goto err_calloc;
			
			conf->key = strdup(key);
			conf->value = strdup(value);
			
			struct freedv_eth_config **entry;
			
			for (entry = &config_list; *entry; entry = &(*entry)->next);
			*entry = conf;
		}
	} while (rf);

	fclose(fd);

	return 0;

err_calloc:
	fclose(fd);
err_fopen:
	return -1;
}

char *freedv_eth_config_value(char *key, char *prev_value, char *def)
{
	struct freedv_eth_config *entry;
	
	for (entry = config_list; entry; entry = entry->next) {
		if (prev_value && entry->value != prev_value)
			continue;
		if (prev_value) {
			prev_value = NULL;
			continue;
		}
		if (!strcmp(entry->key, key))
			return entry->value;
	}
	
	return def;
}
