/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2016 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "data.h"
#include <iostream>
#include <iomanip>

using namespace std;

static bool error = false;

void verify(bool expectFailMatch, string type, string input,
		bool match, string expectStr, string gotStr)
{
	match = match && expectStr == gotStr;
	if (expectFailMatch) {
		if (match) {
			cout << "  failed " << type << " match >" << input
			        << "< error: unexpectedly succeeded" << endl;
			error = true;
		} else {
			cout << "  failed " << type << " match >" << input << "< OK" << endl;
		}
	} else if (match) {
		cout << "  " << type << " match >" << input << "< OK" << endl;
	} else {
		cout << "  " << type << " match >" << input << "< error: got >"
		        << gotStr << "<, expected >" << expectStr << "<" << endl;
			error = true;
	}
}

int main()
{
	string checks[][5] = {
		// entry: definition, decoded value, master data, slave data, flags
		// definition: name,part,type[:len][,[divisor|values][,[unit][,[comment]]]]
		{"x,,ign:10",  "",                              "10fe07000a00000000000000000000", "00", ""},
		{"x,,ign:*",   "",                              "10fe07000a00000000000000000000", "00", "W"},
		{"x,,ign,2",   "",                              "",                               "",   "c"},
		{"x,,str:10",  "Hallo, Du!",                    "10fe07000a48616c6c6f2c20447521", "00", ""},
		{"x,,str:10",  "Hallo, Du!",                    "10fe07000a48616c6c6f2c20447521", "00", ""},
		{"x,,str:10",  "Hallo, Du ",                    "10fe07000a48616c6c6f2c20447520", "00", ""},
		{"x,,str:10",  "          ",                    "10fe07000a20202020202020202020", "00", ""},
		{"x,,str:10",  "",                              "10fe07000a20202020202020202020", "00", "R"},
		{"x,,str:11",  "",                              "10fe07000a20202020202020202020", "00", "rW"},
		{"x,,str:24",  "abcdefghijklmnopqrstuvwx",      "10fe0700186162636465666768696a6b6c6d6e6f707172737475767778", "00", ""},
		{"x,,str:*",   "abcde",                         "10fe0700056162636465",           "00", ""},
		{"x,,str,2",   "",                              "",                               "",   "c"},
		{"x,,nts:10",  "Hallo, Du!",                    "10fe07000a48616c6c6f2c20447521", "00", ""},
		{"x,,nts:10",  "Hallo, Du!",                    "10fe07000a48616c6c6f2c20447521", "00", ""},
		{"x,,nts:10",  "Hallo, Du",                     "10fe07000a48616c6c6f2c20447500", "00", ""},
		{"x,,nts:10",  "          ",                    "10fe07000a20202020202020202020", "00", ""},
		{"x,,nts:10",  "",                              "10fe07000a00000000000000000000", "00", ""},
		{"x,,nts:10",  "abc",                           "10fe07000a6162630065666768696a", "00", "W"},
		{"x,,nts:11",  "",                              "10fe07000a20202020202020202020", "00", "rW"},
		{"x,,nts:24",  "abcdefghijklmnopqrstuvwx",      "10fe0700186162636465666768696a6b6c6d6e6f707172737475767778", "00", ""},
		{"x,,nts:*",   "abcde",                         "10fe0700056162636465",           "00", "W"},
		{"x,,nts:*",   "abcde",                         "10fe070006616263646500",         "00", ""},
		{"x,,nts,2",   "",                              "",                               "",   "c"},
		{"x,,hex",     "20",                            "10fe07000120",                   "00", ""},
		{"x,,hex:10",  "48 61 6c 6c 6f 2c 20 44 75 21", "10fe07000a48616c6c6f2c20447521", "00", ""},
		{"x,,hex:*",   "48 61 6c 6c 6f",                "10fe07000548616c6c6f",           "00", ""},
		{"x,,hex:11",  "",                              "10fe07000a48616c6c6f2c20447521", "00", "rW"},
		{"x,,hex,2",   "",                              "",                               "",   "c"},
		{"x,,bda",   "26.10.2014","10fe07000426100614", "00", ""}, // Sunday
		{"x,,bda",   "01.01.2000","10fe07000401010500", "00", ""}, // Saturday
		{"x,,bda",   "31.12.2099","10fe07000431120399", "00", ""}, // Thursday
		{"x,,bda",   "-.-.-",     "10fe070004ffff00ff", "00", ""},
		{"x,,bda",   "",          "10fe07000432100014", "00", "rw"},
		{"x,,bda:3", "26.10.2014","10fe070003261014",   "00", ""},
		{"x,,bda:3", "01.01.2000","10fe070003010100",   "00", ""},
		{"x,,bda:3", "31.12.2099","10fe070003311299",   "00", ""},
		{"x,,bda:3", "-.-.-",     "10fe070003ffffff",   "00", ""},
		{"x,,bda:3", "",          "10fe070003321299",   "00", "rw"},
		{"x,,bda,2", "",          "",                   "",   "c"},
		{"x,,hda",   "26.10.2014","10fe0700041a0a070e", "00", ""}, // Sunday
		{"x,,hda",   "01.01.2000","10fe07000401010600", "00", ""}, // Saturday
		{"x,,hda",   "31.12.2099","10fe0700041f0c0463", "00", ""}, // Thursday
		{"x,,hda",   "-.-.-",     "10fe070004ffff00ff", "00", ""},
		{"x,,hda",   "",          "10fe070004200c0463", "00", "rw"},
		{"x,,hda:3", "26.10.2014","10fe0700031a0a0e",   "00", ""},
		{"x,,hda:3", "01.01.2000","10fe070003010100",   "00", ""},
		{"x,,hda:3", "31.12.2099","10fe0700031f0c63",   "00", ""},
		{"x,,hda:3", "-.-.-",     "10fe070003ffffff",   "00", ""},
		{"x,,hda:3", "",          "10fe070003200c63",   "00", "rw"},
		{"x,,hda,2", "",          "",                   "",   "c"},
		{"x,,bti",   "21:04:58",  "10fe070003580421",   "00", ""},
		{"x,,bti",   "00:00:00",  "10fe070003000000",   "00", ""},
		{"x,,bti",   "23:59:59",  "10fe070003595923",   "00", ""},
		{"x,,bti",   "",          "10fe070003605923",   "00", "rw"},
		{"x,,bti,2", "",          "",                   "",   "c"},
		{"x,,hti",   "21:04:58",  "10fe07000315043a",   "00", ""},
		{"x,,hti,2", "",          "",                   "",   "c"},
		{"x,,vti",   "21:04:58",  "10fe0700033a0415",   "00", ""},
		{"x,,vti",   "-:-:-",     "10fe070003636363",   "00", ""},
		{"x,,vti,2", "",          "",                   "",   "c"},
		{"x,,btm", "21:04", "10fe0700020421", "00", ""},
		{"x,,btm", "00:00", "10fe0700020000", "00", ""},
		{"x,,btm", "23:59", "10fe0700025923", "00", ""},
		{"x,,btm", "24:00", "10fe0700020024", "00", ""},
		{"x,,btm", "-:-",   "10fe070002ffff", "00", ""},
		{"x,,btm", "",      "10fe0700025924", "00", "rw"},
		{"x,,btm", "24:01", "10fe0700020124", "00", "rw"},
		{"x,,btm,2", "",    "",               "",   "c"},
		{"x,,htm", "21:04", "10fe0700021504", "00", ""},
		{"x,,htm", "00:00", "10fe0700020000", "00", ""},
		{"x,,htm", "23:59", "10fe070002173b", "00", ""},
		{"x,,htm", "24:00", "10fe0700021800", "00", ""},
		{"x,,htm", "",      "10fe070002183b", "00", "rw"},
		{"x,,htm", "24:01", "10fe0700021801", "00", "rw"},
		{"x,,htm,2", "",    "",               "",   "c"},
		{"x,,vtm", "21:04", "10fe0700020415", "00", ""},
		{"x,,vtm", "00:00", "10fe0700020000", "00", ""},
		{"x,,vtm", "23:59", "10fe0700023b17", "00", ""},
		{"x,,vtm", "24:00", "10fe0700020018", "00", ""},
		{"x,,vtm", "-:-",   "10fe070002ffff", "00", ""},
		{"x,,vtm", "",      "10fe0700023b18", "00", "rw"},
		{"x,,vtm", "24:01", "10fe0700020118", "00", "rw"},
		{"x,,vtm,2", "",    "",               "",   "c"},
		{"x,,ttm", "22:40", "10fe07000188",   "00", ""},
		{"x,,ttm", "00:00", "10fe07000100",   "00", ""},
		{"x,,ttm", "23:50", "10fe0700018f",   "00", ""},
		{"x,,ttm", "-:-",   "10fe07000190",   "00", ""},
		{"x,,ttm", "",      "10fe07000191",   "00", "rw"},
		{"x,,ttm,2", "",    "",               "",   "c"},
		{"x,,tth", "22:30", "10fe0700012d",   "00", ""},
		{"x,,tth", "00:30", "10fe07000101",   "00", ""},
		{"x,,tth", "24:00", "10fe07000130",   "00", ""},
		{"x,,tth", "-:-",   "10fe07000100",   "00", ""},
		{"x,,tth", "",      "10fe07000131",   "00", "rw"},
		{"x,,tth,2", "",    "",               "",   "c"},
		{"x,,bdy", "Mon",   "10fe07000100",   "00", ""},
		{"x,,bdy", "Sun",   "10fe07000106",   "00", ""},
		{"x,,bdy", "8",     "10fe07000108",   "00", "w"},
		{"x,,hdy", "Mon",   "10fe07000101",   "00", ""},
		{"x,,hdy", "Sun",   "10fe07000107",   "00", ""},
		{"x,,hdy", "8",     "10fe07000108",   "00", "w"},
		{"x,,pin","1234",   "10feffff021234", "00", ""},
		{"x,,pin","0000",   "10feffff020000", "00", ""},
		{"x,,pin","9999",   "10feffff029999", "00", ""},
		{"x,,pin", "",      "10feffff020000", "00", "Rw"},
		{"x,,pin", "100",   "10feffff020100", "00", "R"},
		{"x,,bcd", "26",    "10feffff0126", "00", ""},
		{"x,,bcd", "0",     "10feffff0100", "00", ""},
		{"x,,bcd", "99",    "10feffff0199", "00", ""},
		{"x,,bcd", "100",   "10feffff0199", "00", "Rw"},
		{"x,,bcd", "-",     "10feffff01ff", "00", ""},
		{"x,,bcd", "",      "10feffff019a", "00", "rw"},
		{"x,,bcd:1","26",   "10feffff0126", "00", ""},
		{"x,,bcd:1","0",    "10feffff0100", "00", ""},
		{"x,,bcd:1","99",   "10feffff0199", "00", ""},
		{"x,,bcd:1","100",  "10feffff0199", "00", "Rw"},
		{"x,,bcd:1","-",    "10feffff01ff", "00", ""},
		{"x,,bcd:1","",     "10feffff019a", "00", "rw"},
		{"x,,bcd:2","126",  "10feffff022601", "00", ""},
		{"x,,bcd:2","0",    "10feffff020000", "00", ""},
		{"x,,bcd:2","9999", "10feffff029999", "00", ""},
		{"x,,bcd:2","10000","10feffff029999", "00", "Rw"},
		{"x,,bcd:2","-",    "10feffff02ffff", "00", ""},
		{"x,,bcd:2","",     "10feffff029a00", "00", "rw"},
		{"x,,bcd:3","12346",  "10feffff03462301", "00", ""},
		{"x,,bcd:3","0",      "10feffff03000000", "00", ""},
		{"x,,bcd:3","999999", "10feffff03999999", "00", ""},
		{"x,,bcd:3","1000000","10feffff03999999", "00", "Rw"},
		{"x,,bcd:3","-",      "10feffff03ffffff", "00", ""},
		{"x,,bcd:3","",       "10feffff03009a00", "00", "rw"},
		{"x,,bcd:4","1234567",  "10feffff0467452301", "00", ""},
		{"x,,bcd:4","0",        "10feffff0400000000", "00", ""},
		{"x,,bcd:4","99999999", "10feffff0499999999", "00", ""},
		{"x,,bcd:4","100000000","10feffff0499999999", "00", "Rw"},
		{"x,,bcd:4","-",        "10feffff04ffffffff", "00", ""},
		{"x,,bcd:4","",         "10feffff0400009a00", "00", "rw"},
		{"x,,hcd:1", "26",     "10feffff011a", "00", ""},
		{"x,,hcd:1", "0",      "10feffff0100", "00", ""},
		{"x,,hcd:1", "99",     "10feffff0163", "00", ""},
		{"x,,hcd:1", "100",    "10feffff0163", "00", "Rw"},
		{"x,,hcd:1", "",       "10feffff019a", "00", "rw"},
		{"x,,hcd:2", "126",    "10feffff021a01", "00", ""},
		{"x,,hcd:2", "0",      "10feffff020000", "00", ""},
		{"x,,hcd:2", "9999",   "10feffff026363", "00", ""},
		{"x,,hcd:2", "10000",  "10feffff026363", "00", "Rw"},
		{"x,,hcd:2", "",       "10feffff029a00", "00", "rw"},
		{"x,,hcd:3","12346",   "10feffff032e1701", "00", ""},
		{"x,,hcd:3","0",       "10feffff03000000", "00", ""},
		{"x,,hcd:3","999999",  "10feffff03636363", "00", ""},
		{"x,,hcd:3","1000000", "10feffff03636363", "00", "Rw"},
		{"x,,hcd:3","",        "10feffff03009a00", "00", "rw"},
		{"x,,hcd:4","1234567",  "10feffff04432d1701", "00", ""},
		{"x,,hcd:4","0",        "10feffff0400000000", "00", ""},
		{"x,,hcd:4","99999999", "10feffff0463636363", "00", ""},
		{"x,,hcd:4","100000000","10feffff0463636363", "00", "Rw"},
		{"x,,hcd","",         "10feffff0400006400", "00", "rw"},
		{"x,,hcd","1234567",  "10feffff04432d1701", "00", ""},
		{"x,,hcd","0",        "10feffff0400000000", "00", ""},
		{"x,,hcd","99999999", "10feffff0463636363", "00", ""},
		{"x,,hcd","100000000", "10feffff0463636363", "00", "Rw"},
		{"x,,hcd","",         "10feffff0400006400", "00", "rw"},
		{"x,,str:16", "0123456789ABCDEF",  "10feffff1130313233343536373839414243444546", "00", ""},
		{"x,,uch:17", "",    "10feffff00", "00", "c"},
		{"x,s,uch", "0",     "1025ffff0310111213", "0300010203", "W"},
		{"x,s,uch", "0",     "1025ffff00", "0100", ""},
		{"x,s,uch,,,,y,m,uch", "3;2","1025ffff0103", "0102", ""},
		{"x,,uch", "38",     "10feffff0126", "00", ""},
		{"x,,uch", "38.5",   "10feffff0126", "00", "R"},
		{"x,,uch", "0",      "10feffff0100", "00", ""},
		{"x,,uch", "254",    "10feffff01fe", "00", ""},
		{"x,,uch", "-",      "10feffff01ff", "00", ""},
		{"x,,uch,10", "3.8", "10feffff0126", "00", ""},
		{"x,,uch,-10", "380","10feffff0126", "00", ""},
		{"x,,sch", "-90",    "10feffff01a6", "00", ""},
		{"x,,sch", "0",      "10feffff0100", "00", ""},
		{"x,,sch", "-1",     "10feffff01ff", "00", ""},
		{"x,,sch", "-",      "10feffff0180", "00", ""},
		{"x,,sch", "-127",   "10feffff0181", "00", ""},
		{"x,,sch", "127",    "10feffff017f", "00", ""},
		{"x,,sch,10", "-9.0","10feffff01a6", "00", ""},
		{"x,,sch,-10","-900","10feffff01a6", "00", ""},
		{"x,,d1b", "-90",    "10feffff01a6", "00", ""},
		{"x,,d1b", "0",      "10feffff0100", "00", ""},
		{"x,,d1b", "-1",     "10feffff01ff", "00", ""},
		{"x,,d1b", "-",      "10feffff0180", "00", ""},
		{"x,,d1b", "-127",   "10feffff0181", "00", ""},
		{"x,,d1b", "127",    "10feffff017f", "00", ""},
		{"x,,d1b,-10","-900","10feffff01a6", "00", ""},
		{"x,,d1c", "19.5",   "10feffff0127", "00", ""},
		{"x,,d1c", "0.0",    "10feffff0100", "00", ""},
		{"x,,d1c", "100.0",  "10feffff01c8", "00", ""},
		{"x,,d1c", "-",      "10feffff01ff", "00", ""},
		{"x,,uin", "38",     "10feffff022600", "00", ""},
		{"x,,uin", "0",      "10feffff020000", "00", ""},
		{"x,,uin", "65534",  "10feffff02feff", "00", ""},
		{"x,,uin", "-",      "10feffff02ffff", "00", ""},
		{"x,,uin,10", "3.8", "10feffff022600", "00", ""},
		{"x,,uin,-10","380", "10feffff022600", "00", ""},
		{"x,,uir", "38",     "10feffff020026", "00", ""},
		{"x,,uir", "0",      "10feffff020000", "00", ""},
		{"x,,uir", "65534",  "10feffff02fffe", "00", ""},
		{"x,,uir", "-",      "10feffff02ffff", "00", ""},
		{"x,,uir,10", "3.8", "10feffff020026", "00", ""},
		{"x,,uir,-10","380", "10feffff020026", "00", ""},
		{"uin10,uin,-10","", "", "", "t"},                  // template
		{"x,,uin10","380",   "10feffff022600", "00", ""},   // template reference
		{"x,,uin10,-10","3800","10feffff022600", "00", ""}, // template reference, valid divider product
		{"x,,uin10,10","","", "", "c"},                     // template reference, invalid divider product
		{"x,,sin", "-90",    "10feffff02a6ff", "00", ""},
		{"x,,sin", "0",      "10feffff020000", "00", ""},
		{"x,,sin", "-1",     "10feffff02ffff", "00", ""},
		{"x,,sin", "-",      "10feffff020080", "00", ""},
		{"x,,sin", "-32767", "10feffff020180", "00", ""},
		{"x,,sin", "32767",  "10feffff02ff7f", "00", ""},
		{"x,,sin,10","-9.0", "10feffff02a6ff", "00", ""},
		{"x,,sin,-10","-900","10feffff02a6ff", "00", ""},
		{"x,,sir", "-90",    "10feffff02ffa6", "00", ""},
		{"x,,sir", "0",      "10feffff020000", "00", ""},
		{"x,,sir", "-1",     "10feffff02ffff", "00", ""},
		{"x,,sir", "-",      "10feffff028000", "00", ""},
		{"x,,sir", "-32767", "10feffff028001", "00", ""},
		{"x,,sir", "32767",  "10feffff027fff", "00", ""},
		{"x,,sir,10","-9.0", "10feffff02ffa6", "00", ""},
		{"x,,sir,-10","-900","10feffff02ffa6", "00", ""},
		{"x,,flt", "-0.090", "10feffff02a6ff", "00", ""},
		{"x,,flt", "0.000",  "10feffff020000", "00", ""},
		{"x,,flt", "-0.001", "10feffff02ffff", "00", ""},
		{"x,,flt", "-",      "10feffff020080", "00", ""},
		{"x,,flt","-32.767", "10feffff020180", "00", ""},
		{"x,,flt", "32.767", "10feffff02ff7f", "00", ""},
		{"x,,flr", "-0.090", "10feffff02ffa6", "00", ""},
		{"x,,flr", "0.000",  "10feffff020000", "00", ""},
		{"x,,flr", "-0.001", "10feffff02ffff", "00", ""},
		{"x,,flr", "-",      "10feffff028000", "00", ""},
		{"x,,flr","-32.767", "10feffff028001", "00", ""},
		{"x,,flr", "32.767", "10feffff027fff", "00", ""},
		{"x,,exp", "-0.09",  "10feffff04ec51b8bd", "00", ""},
		{"x,,exp", "0.0",    "10feffff0400000000", "00", ""},
		{"x,,exp", "-0.001", "10feffff046f1283ba", "00", ""},
		{"x,,exp", "-",      "10feffff040000807f", "00", ""},
		{"x,,exp","-32.767", "10feffff04681103c2", "00", ""},
		{"x,,exp,1000", "-0.000090000",  "10feffff04ec51b8bd", "00", ""},
		{"x,,exp,-100", "-9",  "10feffff04ec51b8bd", "00", ""},
		{"x,,exr", "-0.09",  "10feffff04bdb851ec", "00", ""},
		{"x,,exr", "0.0",    "10feffff0400000000", "00", ""},
		{"x,,exr", "-0.001", "10feffff04ba83126f", "00", ""},
		{"x,,exr", "-",      "10feffff047f800000", "00", ""},
		{"x,,exr","-32.767", "10feffff04c2031168", "00", ""},
		{"x,,exr,1000", "-0.000090000",  "10feffff04bdb851ec", "00", ""},
		{"x,,exr,-100", "-9",  "10feffff04bdb851ec", "00", ""},
		{"x,,d2b", "18.004", "10fe0700090112", "00", ""},
		{"x,,d2b", "0.000",  "10feffff020000", "00", ""},
		{"x,,d2b", "-0.004", "10feffff02ffff", "00", ""},
		{"x,,d2b", "-",      "10feffff020080", "00", ""},
		{"x,,d2b","-127.996","10feffff020180", "00", ""},
		{"x,,d2b", "127.996","10feffff02ff7f", "00", ""},
		{"x,,d2c", "288.06", "10fe0700090112", "00", ""},
		{"x,,d2c", "0.00",   "10feffff020000", "00", ""},
		{"x,,d2c", "-0.06",  "10feffff02ffff", "00", ""},
		{"x,,d2c", "-",      "10feffff020080", "00", ""},
		{"x,,d2c","-2047.94","10feffff020180", "00", ""},
		{"x,,d2c", "2047.94","10feffff02ff7f", "00", ""},
		{"x,,ulg", "38",         "10feffff0426000000", "00", ""},
		{"x,,ulg", "0",          "10feffff0400000000", "00", ""},
		{"x,,ulg", "4294967294", "10feffff04feffffff", "00", ""},
		{"x,,ulg", "-",          "10feffff04ffffffff", "00", ""},
		{"x,,ulg,10","3.8",      "10feffff0426000000", "00", ""},
		{"x,,ulg,-10","380",     "10feffff0426000000", "00", ""},
		{"x,,ulg,0x0FF0F00F   =  VRT 350 ;0x33CCCC33=VRT 360;0x3CC3C33C=SD 17;0x66999966=SD 37;0x69969669=VRT 360+",
		      "VRT 350",         "10feffff040FF0F00F", "00", ""},
		{"x,,ulg,0x=test", "",   "10feffff040FF0F00F", "00", "c"},
		{"x,,ulr", "38",         "10feffff0400000026", "00", ""},
		{"x,,ulr", "0",          "10feffff0400000000", "00", ""},
		{"x,,ulr", "4294967294", "10feffff04fffffffe", "00", ""},
		{"x,,ulr", "-",          "10feffff04ffffffff", "00", ""},
		{"x,,ulr,10","3.8",      "10feffff0400000026", "00", ""},
		{"x,,ulr,-10","380",     "10feffff0400000026", "00", ""},
		{"x,,slg", "-90",        "10feffff04a6ffffff", "00", ""},
		{"x,,slg", "0",          "10feffff0400000000", "00", ""},
		{"x,,slg", "-1",         "10feffff04ffffffff", "00", ""},
		{"x,,slg,10", "-9.0",    "10feffff04a6ffffff", "00", ""},
		{"x,,slg,-10", "-900",   "10feffff04a6ffffff", "00", ""},
		{"x,,slr", "-90",        "10feffff04ffffffa6", "00", ""},
		{"x,,slr", "0",          "10feffff0400000000", "00", ""},
		{"x,,slr", "-1",         "10feffff04ffffffff", "00", ""},
		{"x,,slr,10", "-9.0",    "10feffff04ffffffa6", "00", ""},
		{"x,,slr,-10", "-900",   "10feffff04ffffffa6", "00", ""},
		{"x,,bi3", "1",            "10feffff0108", "00", ""},
		{"x,,bi3", "0",            "10feffff0100", "00", ""},
		{"x,,bi3,0=off;1=on","on", "10feffff0108", "00", ""},
		{"x,,bi3,0=off;1=on","off","10feffff0100", "00", ""},
		{"x,,bi3:2", "1",            "10feffff0108", "00", ""},
		{"x,,bi3:2", "1",            "10feffff01ef", "00", "W"},
		{"x,,bi3:2", "0",            "10feffff0100", "00", ""},
		{"x,,bi3:2", "3",            "10feffff0118", "00", ""},
		{"x,,bi3:2,1=on","on",       "10feffff0108", "00", ""},
		{"x,,bi3:2,1=on","-",        "10feffff0100", "00", ""},
		{"x,,bi3:2,0=off;1=on;2=auto;3=eco","auto", "10feffff0110", "00", ""},
		{"x,,bi3:2,0=off;1=on;2=auto;3=eco","2.5", "10feffff0110", "00", "R"},
		{"x,,bi3:2,0=off;1=on","on", "10feffff0108", "00", ""},
		{"x,,bi3:2,0=off;1=on","off","10feffff0100", "00", ""},
		{"x,,bi3:2,0=off;1=on","1", "10feffff0108", "00", "n"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","x=on ja/nein [Wahrheitswert]", "10feffff0108", "00", "v"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","x=1 ja/nein [Wahrheitswert]", "10feffff0108", "00", "vn"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","\n    \"x\": {\"value\": \"on\"}", "10feffff0108", "00", "j"},
		{",,bi3:2,0=off;1=on,ja/nein,Wahrheitswert", "\n    \"0\": {\"name\": \"\", \"value\": \"on\"}", "10feffff0108", "00", "j"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","\n    \"x\": {\"value\": \"on\", \"unit\": \"ja/nein\", \"comment\": \"Wahrheitswert\"}", "10feffff0108", "00", "vj"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","\n    \"x\": {\"value\": 1}", "10feffff0108", "00", "nj"},
		{"x,,bi3:2,0=off;1=on,ja/nein,Wahrheitswert","\n    \"x\": {\"value\": 1, \"unit\": \"ja/nein\", \"comment\": \"Wahrheitswert\"}", "10feffff0108", "00", "vnj"},
		{"x,,uch,1=test;2=high;3=off;0x10=on","on","10feffff0110", "00", ""},
		{"x,s,uch","3","1050ffff00", "0103", ""},
		{"x,,d2b,,°C,Aussentemperatur","x=18.004 °C [Aussentemperatur]","10fe0700090112", "00", "v"},
		{"x,,bti,,,,y,,bda,,,,z,,bdy", "21:04:58;26.10.2014;Sun","10fe0700085804212610061406", "00", ""}, // combination
		{"x,,bi3,,,,y,,bi5", "1;0",            "10feffff0108", "00", ""}, // bit combination
		{"x,,bi3,,,,y,,bi5", "1;1",            "10feffff0128", "00", ""}, // bit combination
		{"x,,bi3,,,,y,,bi5", "0;1",            "10feffff0120", "00", ""}, // bit combination
		{"x,,bi3,,,,y,,bi5", "0;0",            "10feffff0100", "00", ""}, // bit combination
		{"x,,bi3,,,,y,,bi7,,,,t,,uch", "0;0;9","10feffff020009", "00", ""}, // bit combination
		{"x,,bi6:2,,,,y,,bi0:2,,,,t,,uch", "2;1;9","10feffff03800109", "00", ""}, // bit combination
		{"x,,BI0;BI1;BI2;BI3;BI4;BI5;BI6;BI7", "0;0;1;0;0;0;0;0","ff75b50900", "0104", ""}, // bits
		{"temp,d2b,,°C,Aussentemperatur","","", "", "t"}, // template with relative pos
		{"x,,temp","18.004","10fe0700020112", "00", ""}, // reference to template
		{"x,,temp,10","1.8004","10fe0700020112", "00", ""}, // reference to template, valid divider product
		{"x,,temp,-10","","", "", "c"}, // reference to template, invalid divider product
		{"relrel,d2b,,,,y,d1c","","", "", "t"},   // template struct with relative pos
		{"x,,relrel","18.004;9.5","10fe070003011213", "00", ""}, // reference to template struct
		{"trelrel,temp;temp","","", "", "t"},   // template struct with relative pos and ref to templates
		{"x,,trelrel","18.004;19.008","10fe07000401120213", "00", ""}, // reference to template struct
		{"x,,temp,,,,y,,d1c","18.004;9.5","10fe070003011213", "00", ""}, // reference to template, normal def
		{"x,,temp;HEX:2","18.004;13 14","10fe07000401121314", "00", ""}, // reference to template and base type
	};
	DataFieldTemplates* templates = new DataFieldTemplates();
	DataField* fields = NULL;
	for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
		string check[5] = checks[i];
		istringstream isstr(check[0]);
		string expectStr = check[1];
		SymbolString mstr(false);
		result_t result = mstr.parseHex(check[2]);
		if (result != RESULT_OK) {
			cout << "\"" << check[0] << "\": parse \"" << check[2] << "\" error: " << getResultCode(result) << endl;
			error = true;
			continue;
		}
		SymbolString sstr(false);
		result = sstr.parseHex(check[3]);
		if (result != RESULT_OK) {
			cout << "\"" << check[0] << "\": parse \"" << check[3] << "\" error: " << getResultCode(result) << endl;
			error = true;
			continue;
		}
		string flags = check[4];
		bool isSet = flags.find('s') != string::npos;
		bool failedCreate = flags.find('c') != string::npos;
		bool failedRead = flags.find('r') != string::npos;
		bool failedReadMatch = flags.find('R') != string::npos;
		bool failedWrite = flags.find('w') != string::npos;
		bool failedWriteMatch = flags.find('W') != string::npos;
		bool verbose = flags.find('v') != string::npos;
		bool numeric = flags.find('n') != string::npos;
		bool json = flags.find('j') != string::npos;
		bool isTemplate = flags.find('t') != string::npos;
		string item;
		vector<string> entries;

		while (getline(isstr, item, FIELD_SEPARATOR))
			entries.push_back(item);

		if (fields != NULL) {
			delete fields;
			fields = NULL;
		}
		vector<string>::iterator it = entries.begin();
		result = DataField::create(it, entries.end(), templates, fields, isSet, isTemplate, !isTemplate && (mstr[1]==BROADCAST || isMaster(mstr[1])));
		if (failedCreate) {
			if (result == RESULT_OK) {
				cout << "\"" << check[0] << "\": failed create error: unexpectedly succeeded" << endl;
				error = true;
			} else {
				cout << "\"" << check[0] << "\": failed create OK" << endl;
			}
			continue;
		}
		if (result != RESULT_OK) {
			cout << "\"" << check[0] << "\": create error: " << getResultCode(result) << endl;
			error = true;
			continue;
		}
		if (fields == NULL) {
			cout << "\"" << check[0] << "\": create error: NULL" << endl;
			error = true;
			continue;
		}
		if (it != entries.end()) {
			cout << "\"" << check[0] << "\": create error: trailing input" << endl;
			error = true;
			continue;
		}
		cout << "\"" << check[0] << "\": create OK" << endl;
		if (isTemplate) {
			// store new template
			result = templates->add(fields, "", true);
			if (result == RESULT_OK) {
				fields = NULL;
				cout << "  store template OK" << endl;
			} else {
				cout << "  store template error: " << getResultCode(result) << endl;
				error = true;
			}
			continue;
		}

		ostringstream output;
		SymbolString writeMstr(false);
		result = writeMstr.parseHex(mstr.getDataStr(true, false).substr(0, 10));
		if (result != RESULT_OK) {
			cout << "  parse \"" << mstr.getDataStr(true, false).substr(0, 10) << "\" error: " << getResultCode(result) << endl;
			error = true;
		}
		SymbolString writeSstr(false);
		result = writeSstr.parseHex(sstr.getDataStr(true, false).substr(0, 2));
		if (result != RESULT_OK) {
			cout << "  parse \"" << sstr.getDataStr(true, false).substr(0, 2) << "\" error: " << getResultCode(result) << endl;
			error = true;
		}
		result = fields->read(PartType::masterData, mstr, 0, output, (verbose?OF_VERBOSE:0)|(numeric?OF_NUMERIC:0)|(json?OF_JSON:0), -1, false);
		if (result >= RESULT_OK) {
			result = fields->read(PartType::slaveData, sstr, 0, output, (verbose?OF_VERBOSE:0)|(numeric?OF_NUMERIC:0)|(json?OF_JSON:0), -1, !output.str().empty());
		}
		if (failedRead)
			if (result >= RESULT_OK) {
				cout << "  failed read " << fields->getName() << " >" << check[2] << " " << check[3]
				     << "< error: unexpectedly succeeded" << endl;
				error = true;
			} else {
				cout << "  failed read " << fields->getName() << " >" << check[2] << " " << check[3]
				     << "< OK" << endl;
			}
		else if (result < RESULT_OK) {
			cout << "  read " << fields->getName() << " >" << check[2] << " " << check[3]
			     << "< error: " << getResultCode(result) << endl;
			error = true;
		}
		else {
			bool match = strcasecmp(output.str().c_str(), expectStr.c_str()) == 0;
			verify(failedReadMatch, "read", check[2], match, expectStr, output.str());
		}

		if (!verbose && !json) {
			istringstream input(expectStr);
			result = fields->write(input, PartType::masterData, writeMstr, 0);
			if (result >= RESULT_OK)
				result = fields->write(input, PartType::slaveData, writeSstr, 0);
			if (failedWrite) {
				if (result >= RESULT_OK) {
					cout << "  failed write " << fields->getName() << " >"
					        << expectStr << "< error: unexpectedly succeeded" << endl;
					error = true;
				} else {
					cout << "  failed write " << fields->getName() << " >"
					        << expectStr << "< OK" << endl;
				}
			}
			else if (result < RESULT_OK) {
				cout << "  write " << fields->getName() << " >" << expectStr
				        << "< error: " << getResultCode(result) << endl;
				error = true;
			} else {
				bool match = mstr == writeMstr && sstr == writeSstr;
				verify(failedWriteMatch, "write", expectStr, match, mstr.getDataStr(true, false) + " " + sstr.getDataStr(true, false), writeMstr.getDataStr(true, false) + " " + writeSstr.getDataStr(true, false));
			}
		}
		delete fields;
		fields = NULL;
	}

	delete templates;

	return error ? 1 : 0;
}
