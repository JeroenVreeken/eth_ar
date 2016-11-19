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

#include <eth_ar/fprs.h>
#include "nmea.h"
#include <eth_ar/eth_ar.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

int test_frame_create_destroy(void)
{
	struct fprs_frame *frame;
	
	frame = fprs_frame_create();
	fprs_frame_destroy(frame);
	
	return 0;
}

int test_frame_data_set_get(void)
{
	struct fprs_frame *frame;
	
	frame = fprs_frame_create();

	uint8_t testdata1[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
	fprs_frame_data_set(frame, testdata1, sizeof(testdata1));
	
	uint8_t testdata2[6] = { 0, 0, 0, 0, 0};
	size_t size = 6;
	fprs_frame_data_get(frame, testdata2, &size);
	
	fprs_frame_destroy(frame);
	
	if (size != 5) {
		fprintf(stderr, "Returned size != 5\n");
		return -1;
	}
	if (memcmp(testdata1, testdata2, 5)) {
		fprintf(stderr, "Returned testdata is not equal:\n");
		int i;
		
		for (i = 0; i < 5; i++) {
			fprintf(stderr, "\t0x%02x %s 0x%02x\n",
			    testdata1[i], 
			    testdata1[i] == testdata2[i] ? "==" : "!=", 
			    testdata2[i]);
		}
		return -1;
	}
	
	return 0;
}

int test_frame_elements(void)
{
	struct fprs_frame *frame;
	struct fprs_element *el;
	struct {
		enum fprs_type type;
		size_t size;
		uint8_t *data;
	} testelements[] = {
		{ 15, 1, (uint8_t[]){ 0x02 } },
		{ 16, 4, (uint8_t[]){ 0x01, 0x02, 0x03, 0x04 } },
		{ 273, 2, (uint8_t[]){ 0xaa, 0xbb } },
	};
	
	frame = fprs_frame_create();
	
	uint8_t testdata1[] = { 0x079, 0x02, 0x90, 0x04, 0x01, 0x02, 0x03, 0x04, 0xc1, 0x11, 0x02, 0xaa, 0xbb };
	fprs_frame_data_set(frame, testdata1, sizeof(testdata1));
	
	int elcnt = 0;
	for (el = NULL; (el = fprs_frame_element_get(frame, el));) {
		if (elcnt >= sizeof(testelements)/sizeof(testelements[0])) {
			fprintf(stderr, "Unexpected element\n");
			return -1;
		} else {
			if (testelements[elcnt].type != fprs_element_type(el)) {
				fprintf(stderr, "Type mismatch: %d %d\n",
				    testelements[elcnt].type, fprs_element_type(el));
				return -1;
			}
			if (testelements[elcnt].size != fprs_element_size(el)) {
				fprintf(stderr, "Size mismatch: %zd %zd\n",
				    testelements[elcnt].size, fprs_element_size(el));
				return -1;
			}
			if (fprs_frame_element_by_type(frame, testelements[elcnt].type) != el) {
				fprintf(stderr, "by_type does not find element\n");
				return -1;
			}
			int i;
			
			for (i = 0; i < testelements[elcnt].size; i++) {
				if (testelements[elcnt].data[i] != fprs_element_data(el)[i]) {
					fprintf(stderr, "data mismatch: 0x%02x 0x%02x\n",
					    testelements[elcnt].data[i], fprs_element_data(el)[i]);
					return -1;
				}
			}
		}
		elcnt++;
	}
	if (elcnt != sizeof(testelements)/sizeof(testelements[0])) {
		fprintf(stderr, "Element count wrong\n");
		return -1;
	}
	
	fprs_frame_destroy(frame);
	
	return 0;
}

int test_position(void)
{
	double lat;
	double lon;
	bool fixed;
	int ret = 0;
	int i = 0;
	
	for (lat = -90.0; lat <= 90.0; lat += 0.43) {
		for (lon = -180.0; lon < 180.0; lon += 0.35) {
			i++;
			fixed = i & 1;
			
			struct fprs_frame *frame = fprs_frame_create();
			
			if (fprs_frame_add_position(frame, lon, lat, fixed)) {
				fprintf(stderr, "Could not add position to frame\n");
				return -1;
			}
			
			double lon2, lat2;
			bool fixed2;
			
			struct fprs_element *element = fprs_frame_element_get(frame, NULL);
			if (!element) {
				fprintf(stderr, "Could not retrieve position element\n");
				return -1;
			}

			uint8_t *pos = fprs_element_data(element);
			
			fprs_position_dec(&lon2, &lat2, &fixed2, pos);
			
			if (fixed != fixed2) {
				fprintf(stderr, "fixed bit does not match\n");
				return -1;
			}
			if (fabs(lat - lat2) > 0.000002) {
				fprintf(stderr, "latitude difference to big %e %e %e\n", lat, lat2, fabs(lat - lat2));
				return -1;
			}
			if (fabs(lon - lon2) > 0.000002) {
				fprintf(stderr, "longitude difference to big %e %e %e\n", lon, lon2, fabs(lon - lon2));
				return -1;
			}
			   
			fprs_frame_destroy(frame);
			
		}
	}

	return ret;
}

int test_altitude(void)
{
	double altin;
	double altout;
	int ret = 0;
	
	for (altin = -16384; altin <= 65536 - 16384; altin += 5) {
		struct fprs_frame *frame = fprs_frame_create();
			
		if (fprs_frame_add_altitude(frame, altin)) {
			fprintf(stderr, "Could not add altitude to frame\n");
			return -1;
		}
			
		struct fprs_element *element = fprs_frame_element_get(frame, NULL);
		if (!element) {
			fprintf(stderr, "Could not retrieve altitude element\n");
			return -1;
		}

		uint8_t *alt = fprs_element_data(element);
			
		fprs_altitude_dec(&altout, alt);
			
		if (altin != altout) {
			fprintf(stderr, "%f != %f\n", altout, altin);
			ret++;
		}
			   
		fprs_frame_destroy(frame);
	}

	return ret;
}

int test_callsign(void)
{
	struct fprs_frame *frame = fprs_frame_create();
	uint8_t callsign[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

	if (fprs_frame_add_callsign(frame, callsign)) {
		return -1;
	}

	fprs_frame_destroy(frame);
	return 0;
}

int test_destination(void)
{
	struct fprs_frame *frame = fprs_frame_create();
	uint8_t callsign[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

	if (fprs_frame_add_destination(frame, callsign)) {
		return -1;
	}

	fprs_frame_destroy(frame);
	return 0;
}

int test_request(void)
{
	struct fprs_frame *frame = fprs_frame_create();
	uint8_t callsign[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
	enum fprs_type elements[3] = { 0x00001, 0x0022, 0x0333 };
	int nr_elements = 3;

	if (fprs_frame_add_request(frame, callsign, elements, nr_elements)) {
		return -1;
	}

	struct fprs_element *element = fprs_frame_element_get(frame, NULL);
	if (!element) {
		fprintf(stderr, "Could not retrieve altitude element\n");
		return -1;
	}

	enum fprs_type elements2[4];
	int nr_elements2 = 4;
	uint8_t callsign2[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	
	if (fprs_request_dec(callsign2, elements2, &nr_elements2, 
	    fprs_element_data(element),
	    fprs_element_size(element))) {
		fprintf(stderr, "Could not retrieve request contents\n");
		return -1;
	}
	
	if (memcmp(callsign, callsign2, 6)) {
		fprintf(stderr, "Callsign not identical\n");
		return -1;
	}
	
	if (nr_elements2 != nr_elements) {
		fprintf(stderr, "Incorrect number of elements: %d\n", nr_elements2);
		return -1;	
	}
	
	int i;
	for (i = 0; i < nr_elements; i++) {
		if (elements2[i] != elements[i]) {
			fprintf(stderr, "Elements do not match: %d: %d != %d\n", i, elements2[i], elements[i]);
			return -1;	
		}
	}

	fprs_frame_destroy(frame);
	return 0;
}

int test_objectname(void)
{
	struct fprs_frame *frame = fprs_frame_create();
	char *objectname = "This is an object name, to long for 3 bit lengths";
	
	if (fprs_frame_add_objectname(frame, objectname)) {
		return -1;
	}

	struct fprs_element *element = fprs_frame_element_get(frame, NULL);
	if (!element) {
		return -1;
	}
	uint8_t *fobj = fprs_element_data(element);
	size_t fobj_size = fprs_element_size(element);

	if (!fobj) {
		return -1;
	}
	
	if (fobj_size != strlen(objectname)) {
		return -1;
	}
	if (memcmp(fobj, objectname, strlen(objectname))) {
		return -1;
	}

	fprs_frame_destroy(frame);
	return 0;
}

int vector_valid(double az, double el, double speed, double azt, double elt, double speedt)
{
	while (az >= 360.0)
		az -= 360.0;
	while (az < 0)
		az += 360.0;
	double azd = fabs(az - azt);
	double eld = fabs(el - elt);
	double speedd = fabs(speed - speedt);
	
	if (azd > 0.1 && fabs(azd - 360) > 0.1) {
		printf("az diff: %f %f %f\n", az, azt, azd);
		return 1;
	}
	if (eld > 0.14) {
		printf("el diff: %f %f %f\n", el, elt, eld);
		return 1;
	}
	if (speed > 4) {
		if (speedd / fabs(speed) > 0.016) {
			printf("relative speed diff: %f %f\n", speedd, speedd / fabs(speed));
			return 1;
		}
	} else {
		if (speedd > 0.0625) {
			printf("speed diff: %f\n", speedd);
			return 1;
		}
	}
	
	return 0;
}

int test_vector(void)
{
	uint8_t vector[4];
	int rett = 0;
	
	struct {
		double az;
		double el;
		double speed;
	} tst[] = {
		{ 0, 0, 0 },
		{ 180, 90, 1 },
		{ 270, -90, 2 },
		{ -270, 23, 123 },
		{ 0, 0, 33 },
		{ 0, 0, 100 },
		{ 1, 0, 101 },
		{ 1, 0, 130 },
		{ 1, 0, 260 },
		{ 1, 0, 512 },
		{ 0.1, 0, 0.1 },
		{ 0, 0, 0.2 },
		{ 0, 0, 0.5 },
	};
	
	int i;
	int ret;
	
	for (i = 0; i < sizeof(tst)/sizeof(tst[0]); i++) {
		double az, el, speed;

		struct fprs_frame *frame = fprs_frame_create();
			
		if (fprs_frame_add_vector(frame, tst[i].az, tst[i].el, tst[i].speed)) {
			fprintf(stderr, "Could not add vector to frame\n");
			return -1;
		}

		struct fprs_element *element = fprs_frame_element_get(frame, NULL);
		if (!element) {
			fprintf(stderr, "Could not retrieve position element\n");
			return -1;
		}

		uint8_t *vec = fprs_element_data(element);
			
		fprs_vector_dec(&az, &el, &speed, vec);

		ret = vector_valid(tst[i].az, tst[i].el, tst[i].speed, az, el, speed);
		if (ret)
			printf("%f %f %f -> %f %f %f: %d\n",
			    tst[i].az, tst[i].el, tst[i].speed, az, el, speed, ret);
		rett += ret;

		fprs_frame_destroy(frame);
	}

	double azi = 0, eli = 0, speedi = 0;
	for (azi = 0; azi < 360; azi += 0.01) {
		double az, el, speed;
		
		fprs_vector_enc(vector, azi, eli, speedi);
		fprs_vector_dec(&az, &el, &speed, vector);

		ret = vector_valid(azi, eli, speedi, az, el, speed);
		if (ret)
			printf("%f %f %f -> %f %f %f: %d\n",
			    azi, eli, speedi, az, el, speed, ret);
		rett += ret;
	}
	azi = 0;
	eli = 0;
	speedi = 0;
	for (eli = -90; eli < 90; eli += 0.01) {
		double az, el, speed;
		
		fprs_vector_enc(vector, azi, eli, speedi);
		fprs_vector_dec(&az, &el, &speed, vector);

		ret = vector_valid(azi, eli, speedi, az, el, speed);
		if (ret)
			printf("%f %f %f -> %f %f %f: %d\n",
			    azi, eli, speedi, az, el, speed, ret);
		rett += ret;
	}
	azi = 0;
	eli = 0;
	speedi = 0;
	for (speedi = 0; speedi < 512; speedi += 0.01) {
		double az, el, speed;
		
		fprs_vector_enc(vector, azi, eli, speedi);
		fprs_vector_dec(&az, &el, &speed, vector);

		ret = vector_valid(azi, eli, speedi, az, el, speed);
		if (ret)
			printf("%f %f %f -> %f %f %f: %d\n",
			    azi, eli, speedi, az, el, speed, ret);
		rett += ret;
	}

	return ret;
}

static int test_timestamp(void)
{
	struct fprs_frame *frame = fprs_frame_create();

	if (fprs_frame_add_timestamp(frame, 0x883355)) {
		return -1;
	}

	struct fprs_element *element = fprs_frame_element_get(frame, NULL);
	if (!element) {
		fprintf(stderr, "Could not retrieve position element\n");
		return -1;
	}

	if (fprs_element_size(element) != 3) {
		fprintf(stderr, "Element has wrong size: %zd\n",
		    fprs_element_size(element));
		return -1;
	}

	time_t t;
	if (fprs_timestamp_dec(&t, fprs_element_data(element), fprs_element_size(element))) {
		fprintf(stderr, "Could not retrieve time\n");
		return -1;
	}
	
	if (t != 0x883355) {
		fprintf(stderr, "Wrong time: %llx\n", (long long)t);
		return -1;	
	}

	fprs_frame_destroy(frame);

	return 0;
}

static int test_fprs2aprs(void)
{
	struct fprs_frame *frame;
	double lon, lat;
	bool fixed = false;
	int i = 0;
	uint8_t mac[6];
	
	eth_ar_callssid2mac(mac, "T1EST", false);
	
	for (lon = -180; lon <= 180; lon += 25.7) {
		for (lat = -90; lat <= 90; lat += 35.803) {
			i++;
			fixed = i & 0x1;
			uint8_t *callsign_arg = i & 0x04 ? NULL : mac;
			
			frame = fprs_frame_create();
			if (!frame) {
				fprintf(stderr, "Could not create frame\n");
				return -1;
			}

			if (i & 0x02) {
				uint8_t callsign[6];
				eth_ar_callssid2mac(callsign, "TESTTEST-15", i & 0x01);
				if (fprs_frame_add_callsign(frame, callsign)) {
					fprintf(stderr, "Could not add callsign to frame\n");
					return -1;
				}
			}
			
			if (fprs_frame_add_position(frame, lon, lat, fixed)) {
				fprintf(stderr, "Could not add position to frame\n");
				return -1;
			}
			
			if (i & 0x08) {
				if (fprs_frame_add_vector(frame, lon + i, lat + lon, i)) {
					fprintf(stderr, "Could not add vector to frame\n");
					return -1;
				}
			}

			if (i & 0x10) {
				if (fprs_frame_add_altitude(frame, lat * lon + i)) {
					fprintf(stderr, "Could not add altitude to frame\n");
					return -1;
				}
			}
			
			char aprs[81] = { 0 };
			size_t aprs_size = 80;
			
			if (fprs2aprs(aprs, &aprs_size, frame, callsign_arg, "FPRSGATE"))
				return -1;
		}
	}
	
	return 0;
}

static bool fp_check(double val, double checkval, double prec)
{
	bool r;
	double diff = fabs(val - checkval);
	r = diff <= prec;
	if (!r) {
		printf("%f != %f (within %f)\n", val, checkval, prec);
	}

	return r;
}

static int test_nmea_line(void)
{
	struct nmea_state *state;
	
	state = nmea_state_create();
	if (!state) {
		printf("NMEA state struct create failed\n");
		return -1;
	}
	
	if (nmea_parse_line(state, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47")) {
		printf("Parsing NMEA GGA line failed\n");
		return -1;
	}
	
	if (!state->position_valid || !state->altitude_valid)
		return -1;
			
	if (!fp_check(state->latitude, 48.117301, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->longitude, 11.516667, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->altitude, 545.4, 0.01)) {
		return -1;
	}

	if (nmea_parse_line(state, "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48")) {
		printf("Parsing NMEA VTG line failed\n");
		return -1;
	}
	
	if (!state->course_valid || !state->speed_valid)
		return -1;
	if (!fp_check(state->course, 54.7, 0.01)) {
		return -1;
	}
	if (!fp_check(state->speed, 2.82, 0.01)) {
		return -1;
	}
	
	if (nmea_parse_line(state, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A")) {
		printf("Parsing NMEA RMC line failed\n");
		return -1;
	}
	
	if (!state->position_valid)
		return -1;
			
	if (!fp_check(state->latitude, 48.117301, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->longitude, 11.516667, 0.000001)) {
		return -1;
	}
	if (!state->course_valid || !state->speed_valid)
		return -1;
	if (!fp_check(state->course, 84.4, 0.01)) {
		return -1;
	}
	if (!fp_check(state->speed, 11.52, 0.01)) {
		return -1;
	}
	


	nmea_state_destroy(state);
	
	return 0;
}

static int test_nmea(void)
{
	struct nmea_state *state;
	
	state = nmea_state_create();
	if (!state) {
		printf("NMEA state struct create failed\n");
		return -1;
	}
	
	char *data1 = "blabla*22$GPGSHIT*33$GPGGA,123519,0407.838,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
	char *data2 = "#$@#*33$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";
	char *data3 = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
	
	if (nmea_parse(state, data1, strlen(data1))) {
		printf("Parsing NMEA GGA line failed\n");
		return -1;
	}
	
	if (!state->position_valid || !state->altitude_valid)
		return -1;
			
	if (!fp_check(state->latitude, 4.130633, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->longitude, 11.516667, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->altitude, 545.4, 0.01)) {
		return -1;
	}

	if (nmea_parse(state, data2, strlen(data2))) {
		printf("Parsing NMEA VTG line failed\n");
		return -1;
	}
	
	
	if (!state->course_valid || !state->speed_valid)
		return -1;
	if (!fp_check(state->course, 54.7, 0.01)) {
		return -1;
	}
	if (!fp_check(state->speed, 2.82, 0.01)) {
		return -1;
	}
	
	
	if (nmea_parse(state, data3, strlen(data3))) {
		printf("Parsing NMEA RMC line failed\n");
		return -1;
	}
	
	if (!state->position_valid)
		return -1;
			
	if (!fp_check(state->latitude, 48.117301, 0.000001)) {
		return -1;
	}
	if (!fp_check(state->longitude, 11.516667, 0.000001)) {
		return -1;
	}
	if (!state->course_valid || !state->speed_valid)
		return -1;
	if (!fp_check(state->course, 84.4, 0.01)) {
		return -1;
	}
	if (!fp_check(state->speed, 11.52, 0.01)) {
		return -1;
	}

	nmea_state_destroy(state);
	
	return 0;
}

struct fprs_test {
	char *name;
	int (*func)(void);
} tests[] = {
	{ "Create and destroy frame", test_frame_create_destroy },
	{ "Set and get frame data", test_frame_data_set_get },
	{ "Elements", test_frame_elements },
	{ "POSITION", test_position },
	{ "CALLSIGN", test_callsign },
	{ "DESTINATION", test_destination },
	{ "ALTITUDE", test_altitude },
	{ "OBJECTNAME", test_objectname },
	{ "VECTOR", test_vector },
	{ "TIMESTAMP", test_timestamp },
	{ "REQUEST", test_request },
	{ "fprs2aprs", test_fprs2aprs },
	{ "nmea (line)", test_nmea_line },
	{ "nmea", test_nmea },
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
