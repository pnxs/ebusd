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

#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include "clock.h"

/** the name of each @a LogFacility. */
static const char *facilityNames[] = {
	"main",
	"network",
	"bus",
	"update",
	"all",
	NULL
};

/** the name of each @a LogLevel. */
static const char* levelNames[] = {
	"none",
	"error",
	"notice",
	"info",
	"debug",
	NULL
};

/** the bit combination of currently active log facilities (1 << @a LogFacility). */
static int logFacilites = LF_ALL;

/** the current log level. */
static LogLevel logLevel = ll_notice;

/** the current log FILE. */
static FILE* logFile = stdout;

bool setLogFacilities(const char* facilities)
{
	char *input = strdup(facilities);
	char *opt = (char*)input, *value = NULL;
	int newFacilites = 0;
	while (*opt) {
		int val = getsubopt(&opt, (char *const *)facilityNames, &value);
		if (val < 0 || val > lf_COUNT || value) {
			free(input);
			return false;
		}
		if (val == lf_COUNT)
			newFacilites = LF_ALL;
		else
			newFacilites |= 1<<val;
	}
	logFacilites = newFacilites;
	free(input);
	return true;
}

bool getLogFacilities(char* buffer)
{
	if (logFacilites==LF_ALL)
		return strcpy(buffer, facilityNames[lf_COUNT]) != NULL;
	*buffer = 0; // for strcat to work
	bool found = false;
	for (int val=0; val<lf_COUNT; val++) {
		if (logFacilites&(1<<val)) {
			if (found)
				strcat(buffer, ",");
			found = true;
			strcat(buffer, facilityNames[val]);
		}
	}
	return true;
}

bool setLogLevel(const char* level)
{
	char *input = strdup(level);
	char *opt = (char*)input, *value = NULL;
	int newLevel = 0;
	if (*opt) {
		int val = getsubopt(&opt, (char *const *)levelNames, &value);
		if (val < 0 || val >= ll_COUNT || value || *opt) {
			free(input);
			return false;
		}
		newLevel = val;
	}
	logLevel = (LogLevel)newLevel;
	free(input);
	return true;
}

const char* getLogLevel()
{
	return levelNames[logLevel];
}

bool setLogFile(const char* filename)
{
	FILE* newFile = fopen(filename, "a");
	if (newFile == NULL)
		return false;

	closeLogFile();
	logFile = newFile;
	return true;
}

void closeLogFile()
{
	if (logFile != NULL) {
		if (logFile != stdout)
			fclose(logFile);
		logFile = NULL;
	}
}

bool needsLog(const LogFacility facility, const LogLevel level)
{
	return ((logFacilites & (1<<facility)) != 0)
		&& (logLevel >= level);
}

void logWrite(const LogFacility facility, const LogLevel level, const char* message, ...)
{
	struct timespec ts;
	struct tm* tm;

	clockGettime(&ts);
	tm = localtime(&ts.tv_sec);

	char* buf;
	va_list ap;
	va_start(ap, message);

	if (vasprintf(&buf, message, ap) >= 0 && buf) {
		fprintf(logFile, "%04d-%02d-%02d %02d:%02d:%02d.%03ld [%s %s] %s\n",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec, ts.tv_nsec/1000000,
			facilityNames[facility], levelNames[level], buf);
		fflush(logFile);
	}

	va_end(ap);
	if (buf)
		free(buf);
}
