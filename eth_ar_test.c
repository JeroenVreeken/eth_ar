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

#include <eth_ar/eth_ar.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>


static int test_eth_ar_call2mac(void)
{
	char *call = "test";
	uint8_t mac[6];
	
	if (eth_ar_call2mac(mac, call, 0, false))
		return -1;

	return 0;
}

struct fprs_test {
	char *name;
	int (*func)(void);
} tests[] = {
	{ "eth_ar_call2mac", test_eth_ar_call2mac },
};

int main(int argc, char **argv)
{

	int i;
	int passed = 0;
	int failed = 0;
	
	for (i = 0; i < sizeof(tests)/sizeof(struct fprs_test); i++) {
		int test_ret = tests[i].func();
		
		printf("Test: %s: %s\n", tests[i].name, test_ret ? "Failed" : "Passed");
		if (test_ret)
			failed++;
		else
			passed++;
	}
	
	printf("%d passed, %d failed, Result: %s\n", passed, failed, failed ? "Failed" : "Passed");

	return failed;
}
