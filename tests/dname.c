/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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

#include <config.h>
#include <tests/tap/basic.h>

#include "libknot/dname.h"

/* Test dname_parse_from_wire */
static int test_fw(size_t l, const char *w) {
	const uint8_t *np = (const uint8_t *)w + l;
	return knot_dname_wire_check((const uint8_t *)w, np, NULL) > 0;
}

int main(int argc, char *argv[])
{
	plan(23);
 
	knot_dname_t *d = NULL, *d2 = NULL;
	const char *w = NULL, *t = NULL;
	unsigned len = 0;
	size_t pos = 0;

	/* 1. NULL wire */
	ok(!test_fw(0, NULL), "parsing NULL dname");

	/* 2. empty label */
	ok(test_fw(1, ""), "parsing empty dname");

	/* 3. incomplete dname */
	ok(!test_fw(5, "\x08""dddd"), "parsing incomplete wire");

	/* 4. non-fqdn */
	ok(!test_fw(3, "\x02""ab"), "parsing non-fqdn name");

	/* 5. label > 63b */
	w = "\x40""dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	ok(!test_fw(65, w), "parsing label > 63b");

	/* 6. label count == 126 */
	w = "\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64";
	ok(test_fw(253, w), "parsing label count == 127");

	/* 7. label count == 127 */
	w = "\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64";
	ok(test_fw(255, w), "parsing label count == 127");

	/* 8. label count > 127 */
	w = "\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64";
	ok(!test_fw(257, w), "parsing label count > 127");

	/* 9. dname length > 255 */
	w = "\xff""ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	ok(!test_fw(257, w), "parsing dname len > 255");

	/* 10. special case - invalid label */
	w = "\x20\x68\x6d\x6e\x63\x62\x67\x61\x61\x61\x61\x65\x72\x6b\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x67\x6e\x69\x64\x68\x62\x61\x61\x61\x61\x65\x6c\x64\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x61\x63\x6f\x63\x64\x62\x61\x61\x61\x61\x65\x6b\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x69\x62\x63\x6d\x6a\x6f\x61\x61\x61\x61\x65\x72\x6a\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x6f\x6c\x6e\x6c\x67\x68\x61\x61\x61\x61\x65\x73\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x6a\x6b\x64\x66\x66\x67\x61\x61\x61\x61\x65\x6c\x68\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x67\x67\x6c\x70\x70\x61\x61\x61\x61\x61\x65\x73\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x65\x6b\x6c\x67\x70\x66\x61\x61\x61\x61\x65\x6c\x68\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x0\x21\x42\x63\x84\xa5\xc6\xe7\x8\xa\xd\x11\x73\x3\x6e\x69\x63\x2\x43\x5a";
	ok(!test_fw(277, w), "parsing invalid label (spec. case 1)");

	/* 11. parse from string (correct) .*/
	len = 10;
	w = "\x04""abcd""\x03""efg";
	t = "abcd.efg";
	d = knot_dname_from_str(t, strlen(t));
	ok(d && knot_dname_size(d) == len && memcmp(d, w, len) == 0,
	   "dname_fromstr: parsed correct non-FQDN name");
	knot_dname_free(&d);

	/* 12. parse FQDN from string (correct) .*/
	t = "abcd.efg.";
	d = knot_dname_from_str(t, strlen(t));
	ok(d && knot_dname_size(d) == len && memcmp(d, w, len) == 0,
	   "dname_fromstr: parsed correct FQDN name");
	knot_dname_free(&d);

	/* 13. parse name from string (incorrect) .*/
	t = "..";
	d = knot_dname_from_str(t, strlen(t));
	ok(d == NULL, "dname_fromstr: parsed incorrect name");

	/* 14. equal name is subdomain */
	t = "ab.cd.ef";
	d2 = knot_dname_from_str(t, strlen(t));
	t = "ab.cd.ef";
	d = knot_dname_from_str(t, strlen(t));
	ok(!knot_dname_is_sub(d, d2), "dname_subdomain: equal name");
	knot_dname_free(&d);

	/* 15. true subdomain */
	t = "0.ab.cd.ef";
	d = knot_dname_from_str(t, strlen(t));
	ok(knot_dname_is_sub(d, d2), "dname_subdomain: true subdomain");
	knot_dname_free(&d);

	/* 16. not subdomain */
	t = "cd.ef";
	d = knot_dname_from_str(t, strlen(t));
	ok(!knot_dname_is_sub(d, d2), "dname_subdomain: not subdomain");
	knot_dname_free(&d);

	/* 17. root subdomain */
	t = ".";
	d = knot_dname_from_str(t, strlen(t));
	ok(knot_dname_is_sub(d2, d), "dname_subdomain: root subdomain");
	knot_dname_free(&d);
	knot_dname_free(&d2);

	/* 18-19. dname cat (valid) */
	w = "\x03""cat";
	len = 5;
	d = knot_dname_copy((const uint8_t *)w);
	t = "*";
	d2 = knot_dname_from_str(t, strlen(t));
	d2 = knot_dname_cat(d2, d);
	t = "\x01""*""\x03""cat";
	len = 2 + 4 + 1;
	ok (d2 && len == knot_dname_size(d2), "dname_cat: valid concatenation size");
	ok(memcmp(d2, t, len) == 0, "dname_cat: valid concatenation");
	knot_dname_free(&d);
	knot_dname_free(&d2);

	/* 20-21. parse from wire (valid) */
	t = "\x04""abcd""\x03""efg";
	len = 10;
	pos = 0;
	d = knot_dname_parse((const uint8_t *)t, &pos, len);
	ok(d != NULL, "dname_parse: valid name");
	ok(pos == len, "dname_parse: valid name (parsed length)");
	knot_dname_free(&d);

	/* 22-23. parse from wire (invalid) */
	t = "\x08""dddd";
	len = 5;
	pos = 0;
	d = knot_dname_parse((const uint8_t *)t, &pos, len);
	ok(d == NULL, "dname_parse: bad name");
	ok(pos == 0, "dname_parse: bad name (parsed length)");

	return 0;
}
