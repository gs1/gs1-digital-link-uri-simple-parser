/**
 * GS1 Digital Link URI parser
 *
 * @author Copyright (c) 2021-2023 GS1 AISBL.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "gs1dlparser.h"


#ifdef PRNT
#define DEBUG_PRINT(...) do {				\
	printf(__VA_ARGS__);				\
} while (0)
#else
#define DEBUG_PRINT(...)
#endif

#define SIZEOF_ARRAY(x)	(sizeof(x) / sizeof(x[0]))

// Add to the AI buffer without overflowing
#define writeAIbuf(v,l) do {				\
	if (strlen(ctx->aiBuf) + l > GS1_DL_MAX_AI_BUF)	\
		goto fail;				\
	strncat(ctx->aiBuf, v, l);			\
} while (0)


/*
 *  Set of characters that are permissible in URIs, including percent
 *
 */
static const char *uriCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";


/*
 *  List of Digital Link primary keys
 *
 *  These are used to identify the beginning of the DL pathinfo part.
 *
 *  The list is subject to revision as new identfier keys are introduced.
 *
 */
static const char* dl_pkeys[] = {
	"00",		// SSCC
	"01",		// GTIN
	"253",		// GDTI
	"255",		// GCN
	"401",		// GINC
	"402",		// GSIN
	"414",		// LOC NO.
	"417",		// PARTY
	"8003",		// GRAI
	"8004",		// GIAI
	"8006",		// ITIP
	"8010",		// CPID
	"8013",		// GMN
	"8017",		// GSRN - PROVIDER
	"8018",		// GSRN - RECIPIENT
};

static bool isDLpkey(const char* ai, size_t ailen) {
	size_t i;
	DEBUG_PRINT("        Checking if (%.*s) is a DL primary key\n", (int)ailen, ai);
	for (i = 0; i < SIZEOF_ARRAY(dl_pkeys); i++)
		if (strlen(dl_pkeys[i]) == ailen &&
		    strncmp(ai, dl_pkeys[i], ailen) == 0)
			return true;
	return false;
}


/*
 *  AI prefixes that are defined as not requiring termination by an FNC1
 *  character
 *
 *  Used to separate AI elements in unbracked elements strings and to sort the
 *  components of an element string.
 *
 *  The list is defined by various standards to be immutable, however changes
 *  are not unprecedented.
 *
 */
static const char *fixedAIprefixes[] = {
	"00", "01", "02",
	"03", "04",
	"11", "12", "13", "14", "15", "16", "17", "18", "19",
	"20",
	"31", "32", "33", "34", "35", "36",
	"41"
};

static bool isFNC1required(const char *ai) {
	size_t i;
	for (i = 0; i < SIZEOF_ARRAY(fixedAIprefixes); i++)
		if (strncmp(fixedAIprefixes[i], ai, 2) == 0)
			return false;
	return true;
}


/*
 *  True iff the first len characters of str are all digits
 *
 */
static bool allDigits(const char *str, size_t len) {
	size_t i;
	for (i = 0; i < len; i++)
		if (str[i] < '0' || str[i] >'9')
			return false;
	return true;
}


/*
 *  Decode a percent-encoded input
 *
 */
static size_t URIunescape(char *out, size_t maxlen, const char *in, const size_t inlen, bool is_query_component) {

	size_t i, j;
	char hex[3] = { 0 };

	for (i = 0, j = 0; i < inlen && j < maxlen; i++, j++) {
		if (i < inlen - 2 && in[i] == '%' && isxdigit(in[i+1]) && isxdigit(in[i+2])) {
			hex[0] = in[i+1];
			hex[1] = in[i+2];
			out[j] = (char)strtoul(hex, NULL, 16);
			i += 2;
		} else if (is_query_component && in[i] == '+')
			out[j] = ' ';
		else {
			out[j] = in[i];
		}
	}
	out[j] = '\0';

	return j;

}


bool gs1_parseDLuri(struct gs1DLparser *ctx, char *dlData) {

	char *p, *r, *e, *ai, *outai, *outval;
	char *pi = NULL;			// Path info
	char *qp = NULL;			// Query params
	char *fr = NULL;			// Fragment
	char *dp = NULL;			// DL path info
	bool ret;
	size_t i;
	size_t ailen, vallen;
	char aival[GS1_DL_MAX_AI_LEN+1];	// Unescaped AI value

	ctx->numAIs = 0;
	*ctx->aiBuf = '\0';
	*ctx->err = '\0';

	DEBUG_PRINT("\nParsing DL data: %s\n", dlData);

	p = dlData;

	if (strspn(p, uriCharacters) != strlen(p)) {
		strcpy(ctx->err, "URI contains illegal characters");
		goto fail;
	}

	if (strlen(p) >= 8 && strncmp(p, "https://", 8) == 0)
		p += 8;
	else if (strlen(p) >= 7 && strncmp(p, "http://", 7) == 0)
		p += 7;
	else {
		strcpy(ctx->err, "Scheme must be http:// or https://");
		goto fail;
	}

	DEBUG_PRINT("  Scheme %.*s\n", (int)(p-dlData-3), dlData);

	if (((r = strchr(p, '/')) == NULL) || r-p < 1) {
		strcpy(ctx->err, "URI must contain a domain and path info");
		goto fail;
	}

	DEBUG_PRINT("  Domain: %.*s\n", (int)(r-p), p);

	pi = p = r;					// Skip the domain name

	// Fragment character delimits end of data
	if ((fr = strchr(pi, '#')) != NULL)
		*fr++ = '\0';

	// Query parameter marker delimits end of path info
	if ((qp = strchr(pi, '?')) != NULL)
		*qp++ = '\0';

	DEBUG_PRINT("  Path info: %s\n", pi);
	DEBUG_PRINT("    Searching path info backwards for Digital Link primary key\n");

	// Search backwards from the end of the path info looking for an
	// "/AI/value" pair where AI is a DL primary key
	while ((r = strrchr(pi, '/')) != NULL) {

		*p = '/';				// Restore original pair separator
							// Clobbers first character of path
							// info on first iteration

		// Find start of AI
		*r = '\0';				// Chop off value
		p = strrchr(pi, '/'); 			// Beginning of AI
		*r = '/';				// Restore original AI/value separator
		if (!p)					// At beginning of path
			break;

		DEBUG_PRINT("      %s\n", p);

		ailen = (size_t)(r-p-1);
		if (ailen < 2 || ailen > 4 || !allDigits(p+1, ailen)) {
			DEBUG_PRINT("        Stopping. (%.*s) is not a valid form for an AI.\n", (int)ailen, p+1);
			break;
		}

		if (isDLpkey(p+1, ailen)) {		// Found root of DL path info
			dp = p;
			break;
		}

		*p = '\0';

	}

	if (!dp) {
		strcpy(ctx->err, "No GS1 DL keys found in path info");
		goto fail;
	}

	DEBUG_PRINT("  Stem: %.*s\n", (int)(dp-dlData), dlData);

	DEBUG_PRINT("  Processing DL path info part: %s\n", dp);

	// Process each AI value pair in the DL path info
	p = dp;
	while (*p) {
		p++;
		r = strchr(p, '/');

		// AI is known to be valid since we previously walked over it
		ai = p;
		ailen = (size_t)(r-p);

		if ((p = strchr(++r, '/')) == NULL)
			p = r + strlen(r);

		if (p == r) {
			snprintf(ctx->err, sizeof(ctx->err), "AI (%.*s) value path element is empty", (int)ailen, ai);
			goto fail;
		}

		// Reverse percent encoding
		if ((vallen = URIunescape(aival, GS1_DL_MAX_AI_LEN, r, (size_t)(p-r), false)) == 0) {
			sprintf(ctx->err, "Decoded AI (%.*s) from DL path info too long", (int)ailen, ai);
			goto fail;
		}

		// Special handling of AI (01) to pad up to a GTIN-14
		if (ailen == 2 && strncmp(ai, "01", 2) == 0 &&
		    (vallen == 13 || vallen == 12 || vallen == 8)) {
			for (i = 0; i <= 13; i++)
				aival[13-i] = vallen >= i+1 ? aival[vallen-i-1] : '0';
			aival[14] = '\0';
			vallen = 14;
		}

		DEBUG_PRINT("    Extracted: (%.*s) %.*s\n", (int)ailen, ai, (int)vallen, aival);

		outai = ctx->aiBuf + strlen(ctx->aiBuf);	// Save start of AI for AI data
		writeAIbuf(ai, ailen);				// Write AI
		outval = ctx->aiBuf + strlen(ctx->aiBuf);	// Save start of value for AI data
		writeAIbuf(aival, vallen);			// Write value

		// Update the AI data
		if (ctx->numAIs < GS1_DL_MAX_AIS) {
			ctx->aiData[ctx->numAIs].ai = outai;
			ctx->aiData[ctx->numAIs].ailen = (short)ailen;
			ctx->aiData[ctx->numAIs].value = outval;
			ctx->aiData[ctx->numAIs].vallen = (short)vallen;
			ctx->aiData[ctx->numAIs].fnc1 = isFNC1required(outai);
			ctx->numAIs++;
		} else {
			strcpy(ctx->err, "Too many AIs");
			goto fail;
		}
	}

	if (qp) {
		DEBUG_PRINT("  Processing query params: %s\n", qp);
	}

	p = qp;
	while (p && *p) {

		while (*p == '&')				// Jump any & separators
			p++;
		if ((r = strchr(p, '&')) == NULL)
			r = p + strlen(p);			// Value-pair finishes at end of data

		// Discard parameters with no value
		if ((e = memchr(p, '=', (size_t)(r-p))) == NULL) {
			DEBUG_PRINT("    Skipped singleton:   %.*s\n", (int)(r-p), p);
			p = r;
			continue;
		}

		// Numeric-only query parameters not matching valid form of an AI aren't permitted
		ai = p;
		ailen = (size_t)(e-p);
		if (allDigits(p, ailen)) {
			if (ailen < 2 || ailen > 4) {
				sprintf(ctx->err, "Stopping. Numeric query parameter that is not a valid AI is illegal: %.*s...",
					(ailen<10?(int)ailen:10), p);
				goto fail;
			}
		} else {
			// Skip non-numeric query parameters
			DEBUG_PRINT("    Skipped:   %.*s\n", (int)(r-p), p);
			p = r;
			continue;
		}

		e++;
		if (r == e) {
			snprintf(ctx->err, sizeof(ctx->err), "AI (%.*s) value query element is empty", (int)ailen, ai);
			goto fail;
		}

		// Reverse percent encoding
		if ((vallen = URIunescape(aival, GS1_DL_MAX_AI_LEN, e, (size_t)(r-e), true)) == 0) {
			sprintf(ctx->err, "Decoded AI (%.*s) value from DL query params too long", (int)ailen, ai);
			goto fail;
		}

		// Special handling of AI (01) to pad up to a GTIN-14
		if (ailen == 2 && strncmp(ai, "01", 2) == 0 &&
		    (vallen == 13 || vallen == 12 || vallen == 8)) {
			for (i = 0; i <= 13; i++)
				aival[13-i] = vallen >= i+1 ? aival[vallen-i-1] : '0';
			aival[14] = '\0';
			vallen = 14;
		}

		DEBUG_PRINT("    Extracted: (%.*s) %.*s\n", (int)ailen, ai, (int)vallen, aival);

		outai = ctx->aiBuf + strlen(ctx->aiBuf);	// Save start of AI for AI data
		writeAIbuf(ai, ailen);				// Write AI
		outval = ctx->aiBuf + strlen(ctx->aiBuf);	// Save start of value for AI data
		writeAIbuf(aival, vallen);			// Write value

		// Update the AI data
		if (ctx->numAIs < GS1_DL_MAX_AIS) {
			ctx->aiData[ctx->numAIs].ai = outai;
			ctx->aiData[ctx->numAIs].ailen = (short)ailen;
			ctx->aiData[ctx->numAIs].value = outval;
			ctx->aiData[ctx->numAIs].vallen = (short)vallen;
			ctx->aiData[ctx->numAIs].fnc1 = isFNC1required(outai);
			ctx->numAIs++;
		} else {
			strcpy(ctx->err, "Too many AIs");
			goto fail;
		}

		p = r;

	}

	if (fr) {
		DEBUG_PRINT("  Fragment: %s\n", fr);
	}

	DEBUG_PRINT("Parsing DL data successful\n\n");

	ret = true;

out:

	if (qp)			// Restore original query parameter delimiter
		*(qp-1) = '?';

	if (fr)			// Restore original fragment delimiter
		*(fr-1) = '#';

	return ret;

fail:

	if (*ctx->err == '\0')
		strcpy(ctx->err, "Failed to parse DL data");

	DEBUG_PRINT("Parsing DL data failed: %s\n", ctx->err);

	ctx->numAIs = 0;
	ret = false;
	goto out;

}


void gs1_writeUnbracketedAIelementString(struct gs1DLparser *ctx, bool fixedFirst, bool extraFNC1, char *out) {

	int i;
	struct gs1AIelement ai;
	char *p = out;
	bool fixedPass = true;		// First pass extracts predefined fixed-length AIs

	*p++ = '^';

nextPass:

	for (i = 0; i < ctx->numAIs; i++) {
		ai = ctx->aiData[i];

		if (fixedFirst && !(fixedPass ^ ai.fnc1))
			continue;

		p += sprintf(p, "%.*s%.*s", ai.ailen, ai.ai, ai.vallen, ai.value);
		if (extraFNC1 || ai.fnc1)
			*p++ = '^';
	}

	if (fixedFirst && fixedPass) {
		fixedPass = false;
		goto nextPass;
	}

	if (*(p-1) == '^')
		p--;

	*p = '\0';

	return;

}


void gs1_writeBracketedAIelementString(struct gs1DLparser *ctx, bool fixedFirst, char *out) {

	int i, j;
	struct gs1AIelement ai;
	char *p = out;
	bool fixedPass = true;		// First pass extracts predefined fixed-length AIs

nextPass:

	for (i = 0; i < ctx->numAIs; i++) {
		ai = ctx->aiData[i];

		if (fixedFirst && !(fixedPass ^ ai.fnc1))
			continue;

		p += sprintf(p, "(%.*s)", ai.ailen, ai.ai);
		for (j = 0; j < ai.vallen; j++) {
			if (ai.value[j] == '(')	 // Escape data "("
				*p++ = '\\';
			*p++ = ai.value[j];
		}
	}

	if (fixedFirst && fixedPass) {
		fixedPass = false;
		goto nextPass;
	}

	*p = '\0';

	return;

}


void gs1_writeJSON(struct gs1DLparser *ctx, bool fixedFirst, char *out) {

	int i, j;
	struct gs1AIelement ai;
	char *p = out;
	bool fixedPass = true;		// First pass extracts predefined fixed-length AIs

	*p++ = '{';

nextPass:

	for (i = 0; i < ctx->numAIs; i++) {
		ai = ctx->aiData[i];

		if (fixedFirst && !(fixedPass ^ ai.fnc1))
			continue;

		p += sprintf(p, "\"%.*s\":\"", ai.ailen, ai.ai);
		for (j = 0; j < ai.vallen; j++) {
			if (ai.value[j] == '\\' || ai.value[j] == '"')		// Escape backslash and double-quote
				*p++ = '\\';
			*p++ = ai.value[j];
		}
		*p++ = '"';
		*p++ = ',';
	}

	if (fixedFirst && fixedPass) {
		fixedPass = false;
		goto nextPass;
	}

	*--p = '}';	// Gobble last comma
	*++p = '\0';

	return;

}


#ifdef UNIT_TESTS

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-folding-constant"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#endif
#include "acutest.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif


static void test_parseDLuri(struct gs1DLparser *ctx, bool should_succeed, const char *dlData,
			    const char* expect_unbracketed_unsorted,
			    const char* expect_unbracketed_ExtraFNC1_unsorted,
			    const char* expect_bracketed_unsorted,
			    const char* expect_JSON_unsorted,
			    const char* expect_unbracketed_sorted,
			    const char* expect_unbracketed_ExtraFNC1_sorted,
			    const char* expect_bracketed_sorted,
			    const char* expect_JSON_sorted) {

	char in[256];
	char out[256];
	char casename[256];

	sprintf(casename, "%s", dlData);
	TEST_CASE(casename);

	strcpy(in, dlData);

	TEST_CHECK(gs1_parseDLuri(ctx, in) ^ (!should_succeed));
	TEST_MSG("Err: %s", ctx->err);

	TEST_CHECK(strcmp(dlData, in) == 0);
	TEST_MSG("Input data was erroneously clobbered: %s", in);

	if (!should_succeed)
		return;

	gs1_writeUnbracketedAIelementString(ctx, false, false, out);
	TEST_CHECK(strcmp(out, expect_unbracketed_unsorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_unbracketed_unsorted, ctx->err);

	gs1_writeUnbracketedAIelementString(ctx, false, true, out);
	TEST_CHECK(strcmp(out, expect_unbracketed_ExtraFNC1_unsorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_unbracketed_ExtraFNC1_unsorted, ctx->err);

	gs1_writeBracketedAIelementString(ctx, false, out);
	TEST_CHECK(strcmp(out, expect_bracketed_unsorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_bracketed_unsorted, ctx->err);

	gs1_writeJSON(ctx, false, out);
	TEST_CHECK(strcmp(out, expect_JSON_unsorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_JSON_unsorted, ctx->err);

	gs1_writeUnbracketedAIelementString(ctx, true, false, out);
	TEST_CHECK(strcmp(out, expect_unbracketed_sorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_unbracketed_sorted, ctx->err);

	gs1_writeUnbracketedAIelementString(ctx, true, true, out);
	TEST_CHECK(strcmp(out, expect_unbracketed_ExtraFNC1_sorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_unbracketed_ExtraFNC1_sorted, ctx->err);

	gs1_writeBracketedAIelementString(ctx, true, out);
	TEST_CHECK(strcmp(out, expect_bracketed_sorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_bracketed_sorted, ctx->err);

	gs1_writeJSON(ctx, true, out);
	TEST_CHECK(strcmp(out, expect_JSON_sorted) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect_JSON_sorted, ctx->err);

}

static void test_dl_parseDLuri(void) {

	struct gs1DLparser *ctx = malloc(sizeof(struct gs1DLparser));

	/*
	 *  Order of expected output params:
	 *
	 *    - Unbracketed with standard FNC1s
	 *    - Unbracketed with extraneous FNC1s
	 *    - Bracketed syntax
	 *    - JSON
	 *    - Unbracketed with standard FNC1s; fixed-length AIs first
	 *    - Unbracketed with extraneous FNC1s; fixed-length AIs first
	 *    - Bracketed syntax; fixed-length AIs first
	 *    - JSON; fixed-length AIs first
	 *
	 */

	test_parseDLuri(ctx, false,  "", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "ftp://", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "http://", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "http:///", "", "", "", "", "", "", "", "");	// No domain
	test_parseDLuri(ctx, false,  "http://a", "", "", "", "", "", "", "", "");	// No path info
	test_parseDLuri(ctx, false,  "http://a/", "", "", "", "", "", "", "", "");	// Pathelogical minimal domain but no AI info
	test_parseDLuri(ctx, false,  "http://a/b", "", "", "", "", "", "", "", "");	// Stem, no data
	test_parseDLuri(ctx, false,  "http://a/b/", "", "", "", "", "", "", "", "");

	test_parseDLuri(ctx, true,					// http
		"http://a/00/006141411234567890",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, true,					// https
		"https://a/00/006141411234567890",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, false,					// No domain
		"https://00/006141411234567890",
		"", "", "", "", "", "", "", "");

	test_parseDLuri(ctx, true,
		"https://a/stem/00/006141411234567890",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, true,
		"https://a/more/stem/00/006141411234567890",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, true,					// Fake AI in stem, stop at rightmost key
		"https://a/00/faux/00/006141411234567890",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333",
		"^0112312312312333",
		"^0112312312312333",
		"(01)12312312312333",
		"{\"01\":\"12312312312333\"}",
		"^0112312312312333",
		"^0112312312312333",
		"(01)12312312312333",
		"{\"01\":\"12312312312333\"}");

	test_parseDLuri(ctx, true,					// GTIN-13 -> GTIN-14
		"https://a/01/2112345678900",
		"^0102112345678900",
		"^0102112345678900",
		"(01)02112345678900",
		"{\"01\":\"02112345678900\"}",
		"^0102112345678900",
		"^0102112345678900",
		"(01)02112345678900",
		"{\"01\":\"02112345678900\"}");

	test_parseDLuri(ctx, true,					// GTIN-12 -> GTIN-14
		"https://a/01/416000336108",
		"^0100416000336108",
		"^0100416000336108",
		"(01)00416000336108",
		"{\"01\":\"00416000336108\"}",
		"^0100416000336108",
		"^0100416000336108",
		"(01)00416000336108",
		"{\"01\":\"00416000336108\"}");

	test_parseDLuri(ctx, true,					// GTIN-8 -> GTIN-14
		"https://a/01/02345673",
		"^0100000002345673",
		"^0100000002345673",
		"(01)00000002345673",
		"{\"01\":\"00000002345673\"}",
		"^0100000002345673",
		"^0100000002345673",
		"(01)00000002345673",
		"{\"01\":\"00000002345673\"}");

	// Invalid-length AI components
	test_parseDLuri(ctx, false,  "https://a/01/12312312312333/9/abc", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "https://a/01/12312312312333/99999/abc", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "https://a/01/12312312312333?9=abc", "", "", "", "", "", "", "", "");
	test_parseDLuri(ctx, false,  "https://a/01/12312312312333?99999=abc", "", "", "", "", "", "", "", "");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333/22/TEST/10/ABC/21/XYZ",
		"^011231231231233322TEST^10ABC^21XYZ",
		"^0112312312312333^22TEST^10ABC^21XYZ",
		"(01)12312312312333(22)TEST(10)ABC(21)XYZ",
		"{\"01\":\"12312312312333\",\"22\":\"TEST\",\"10\":\"ABC\",\"21\":\"XYZ\"}",
		"^011231231231233322TEST^10ABC^21XYZ",
		"^0112312312312333^22TEST^10ABC^21XYZ",
		"(01)12312312312333(22)TEST(10)ABC(21)XYZ",
		"{\"01\":\"12312312312333\",\"22\":\"TEST\",\"10\":\"ABC\",\"21\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333/235/TEST",
		"^0112312312312333235TEST",
		"^0112312312312333^235TEST",
		"(01)12312312312333(235)TEST",
		"{\"01\":\"12312312312333\",\"235\":\"TEST\"}",
		"^0112312312312333235TEST",
		"^0112312312312333^235TEST",
		"(01)12312312312333(235)TEST",
		"{\"01\":\"12312312312333\",\"235\":\"TEST\"}");

	test_parseDLuri(ctx, true,
		"https://a/253/1231231231232",
		"^2531231231231232",
		"^2531231231231232",
		"(253)1231231231232",
		"{\"253\":\"1231231231232\"}",
		"^2531231231231232",
		"^2531231231231232",
		"(253)1231231231232",
		"{\"253\":\"1231231231232\"}");

	test_parseDLuri(ctx, true,
		"https://a/253/1231231231232TEST5678901234567",
		"^2531231231231232TEST5678901234567",
		"^2531231231231232TEST5678901234567",
		"(253)1231231231232TEST5678901234567",
		"{\"253\":\"1231231231232TEST5678901234567\"}",
		"^2531231231231232TEST5678901234567",
		"^2531231231231232TEST5678901234567",
		"(253)1231231231232TEST5678901234567",
		"{\"253\":\"1231231231232TEST5678901234567\"}");

	test_parseDLuri(ctx, true,
		"https://a/8018/123456789012345675/8019/123",
		"^8018123456789012345675^8019123",
		"^8018123456789012345675^8019123",
		"(8018)123456789012345675(8019)123",
		"{\"8018\":\"123456789012345675\",\"8019\":\"123\"}",
		"^8018123456789012345675^8019123",
		"^8018123456789012345675^8019123",
		"(8018)123456789012345675(8019)123",
		"{\"8018\":\"123456789012345675\",\"8019\":\"123\"}");

	test_parseDLuri(ctx, false,
		"https://a/stem/00/006141411234567890/", "", "", "", "", "", "", "", ""); 	// Can't end in slash

	test_parseDLuri(ctx, true,					// Empty query params
		"https://a/00/006141411234567890?",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}",
		"^00006141411234567890",
		"^00006141411234567890",
		"(00)006141411234567890",
		"{\"00\":\"006141411234567890\"}");

	test_parseDLuri(ctx, true,
		"https://a/stem/00/006141411234567890?99=ABC",			// Query params; no FNC1 req after pathinfo
		"^0000614141123456789099ABC",
		"^00006141411234567890^99ABC",
		"(00)006141411234567890(99)ABC",
		"{\"00\":\"006141411234567890\",\"99\":\"ABC\"}",
		"^0000614141123456789099ABC",
		"^00006141411234567890^99ABC",
		"(00)006141411234567890(99)ABC",
		"{\"00\":\"006141411234567890\",\"99\":\"ABC\"}");

	test_parseDLuri(ctx, true,
		"https://a/stem/401/12345678?99=ABC",				// Query params; FNC1 req after pathinfo
		"^40112345678^99ABC",
		"^40112345678^99ABC",
		"(401)12345678(99)ABC",
		"{\"401\":\"12345678\",\"99\":\"ABC\"}",
		"^40112345678^99ABC",
		"^40112345678^99ABC",
		"(401)12345678(99)ABC",
		"{\"401\":\"12345678\",\"99\":\"ABC\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?99=ABC&98=XYZ",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, false,
		"https://a/01/12312312312333?99=", "", "", "", "", "", "", "", ""); 	// Can't have empty AI value

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?&&&99=ABC&&&&&&98=XYZ&&&",		// Extraneous query param separators
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?99=ABC&unknown=666&98=XYZ",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?99=ABC&singleton&98=XYZ",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?singleton&99=ABC&98=XYZ",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333?99=ABC&98=XYZ&singleton",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}",
		"^011231231231233399ABC^98XYZ",
		"^0112312312312333^99ABC^98XYZ",
		"(01)12312312312333(99)ABC(98)XYZ",
		"{\"01\":\"12312312312333\",\"99\":\"ABC\",\"98\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333/22/ABC%2d123?99=ABC&98=XYZ%2f987",	// Percent escaped values
		"^011231231231233322ABC-123^99ABC^98XYZ/987",
		"^0112312312312333^22ABC-123^99ABC^98XYZ/987",
		"(01)12312312312333(22)ABC-123(99)ABC(98)XYZ/987",
		"{\"01\":\"12312312312333\",\"22\":\"ABC-123\",\"99\":\"ABC\",\"98\":\"XYZ/987\"}",
		"^011231231231233322ABC-123^99ABC^98XYZ/987",
		"^0112312312312333^22ABC-123^99ABC^98XYZ/987",
		"(01)12312312312333(22)ABC-123(99)ABC(98)XYZ/987",
		"{\"01\":\"12312312312333\",\"22\":\"ABC-123\",\"99\":\"ABC\",\"98\":\"XYZ/987\"}");

	test_parseDLuri(ctx, true,
		"https://a/01/12312312312333/22/TEST/10/ABC/21/XYZ#fragmemt",	// Ignore fragment after path info
		"^011231231231233322TEST^10ABC^21XYZ",
		"^0112312312312333^22TEST^10ABC^21XYZ",
		"(01)12312312312333(22)TEST(10)ABC(21)XYZ",
		"{\"01\":\"12312312312333\",\"22\":\"TEST\",\"10\":\"ABC\",\"21\":\"XYZ\"}",
		"^011231231231233322TEST^10ABC^21XYZ",
		"^0112312312312333^22TEST^10ABC^21XYZ",
		"(01)12312312312333(22)TEST(10)ABC(21)XYZ",
		"{\"01\":\"12312312312333\",\"22\":\"TEST\",\"10\":\"ABC\",\"21\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://a/stem/00/006141411234567890?99=ABC#fragment",		// Ignore fragment after query info
		"^0000614141123456789099ABC",
		"^00006141411234567890^99ABC",
		"(00)006141411234567890(99)ABC",
		"{\"00\":\"006141411234567890\",\"99\":\"ABC\"}",
		"^0000614141123456789099ABC",
		"^00006141411234567890^99ABC",
		"(00)006141411234567890(99)ABC",
		"{\"00\":\"006141411234567890\",\"99\":\"ABC\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}");

	test_parseDLuri(ctx, true,
		"https://brand.example.com/01/9520123456788",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}");

	test_parseDLuri(ctx, true,
		"https://brand.example.com/some-extra/pathinfo/01/9520123456788",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}",
		"^0109520123456788",
		"^0109520123456788",
		"(01)09520123456788",
		"{\"01\":\"09520123456788\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788/22/2A",
		"^0109520123456788222A",
		"^0109520123456788^222A",
		"(01)09520123456788(22)2A",
		"{\"01\":\"09520123456788\",\"22\":\"2A\"}",
		"^0109520123456788222A",
		"^0109520123456788^222A",
		"(01)09520123456788(22)2A",
		"{\"01\":\"09520123456788\",\"22\":\"2A\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788/10/ABC123",
		"^010952012345678810ABC123",
		"^0109520123456788^10ABC123",
		"(01)09520123456788(10)ABC123",
		"{\"01\":\"09520123456788\",\"10\":\"ABC123\"}",
		"^010952012345678810ABC123",
		"^0109520123456788^10ABC123",
		"(01)09520123456788(10)ABC123",
		"{\"01\":\"09520123456788\",\"10\":\"ABC123\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788/21/12345",
		"^01095201234567882112345",
		"^0109520123456788^2112345",
		"(01)09520123456788(21)12345",
		"{\"01\":\"09520123456788\",\"21\":\"12345\"}",
		"^01095201234567882112345",
		"^0109520123456788^2112345",
		"(01)09520123456788(21)12345",
		"{\"01\":\"09520123456788\",\"21\":\"12345\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788/10/ABC1/21/12345?17=180426",
		"^010952012345678810ABC1^2112345^17180426",
		"^0109520123456788^10ABC1^2112345^17180426",
		"(01)09520123456788(10)ABC1(21)12345(17)180426",
		"{\"01\":\"09520123456788\",\"10\":\"ABC1\",\"21\":\"12345\",\"17\":\"180426\"}",
		"^01095201234567881718042610ABC1^2112345",
		"^0109520123456788^17180426^10ABC1^2112345",
		"(01)09520123456788(17)180426(10)ABC1(21)12345",
		"{\"01\":\"09520123456788\",\"17\":\"180426\",\"10\":\"ABC1\",\"21\":\"12345\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/09520123456788?3103=000195",
		"^01095201234567883103000195",
		"^0109520123456788^3103000195",
		"(01)09520123456788(3103)000195",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\"}",
		"^01095201234567883103000195",
		"^0109520123456788^3103000195",
		"(01)09520123456788(3103)000195",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\"}");

	test_parseDLuri(ctx, true,
		"https://example.com/01/9520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225",
		"^0109520123456788^3103000195^39220299^17201225",
		"(01)09520123456788(3103)000195(3922)0299(17)201225",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\",\"3922\":\"0299\",\"17\":\"201225\"}",
		"^010952012345678831030001951720122539220299",
		"^0109520123456788^3103000195^17201225^39220299",
		"(01)09520123456788(3103)000195(17)201225(3922)0299",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\",\"17\":\"201225\",\"3922\":\"0299\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/9520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225",
		"^0109520123456788^3103000195^39220299^17201225",
		"(01)09520123456788(3103)000195(3922)0299(17)201225",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\",\"3922\":\"0299\",\"17\":\"201225\"}",
		"^010952012345678831030001951720122539220299",
		"^0109520123456788^3103000195^17201225^39220299",
		"(01)09520123456788(3103)000195(17)201225(3922)0299",
		"{\"01\":\"09520123456788\",\"3103\":\"000195\",\"17\":\"201225\",\"3922\":\"0299\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/01/9520123456788?17=201225&3103=000195&3922=0299",
		"^010952012345678817201225310300019539220299",
		"^0109520123456788^17201225^3103000195^39220299",
		"(01)09520123456788(17)201225(3103)000195(3922)0299",
		"{\"01\":\"09520123456788\",\"17\":\"201225\",\"3103\":\"000195\",\"3922\":\"0299\"}",
		"^010952012345678817201225310300019539220299",
		"^0109520123456788^17201225^3103000195^39220299",
		"(01)09520123456788(17)201225(3103)000195(3922)0299",
		"{\"01\":\"09520123456788\",\"17\":\"201225\",\"3103\":\"000195\",\"3922\":\"0299\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/00/952012345678912345",
		"^00952012345678912345",
		"^00952012345678912345",
		"(00)952012345678912345",
		"{\"00\":\"952012345678912345\"}",
		"^00952012345678912345",
		"^00952012345678912345",
		"(00)952012345678912345",
		"{\"00\":\"952012345678912345\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/00/952012345678912345?02=09520123456788&37=25&10=ABC123",
		"^0095201234567891234502095201234567883725^10ABC123",
		"^00952012345678912345^0209520123456788^3725^10ABC123",
		"(00)952012345678912345(02)09520123456788(37)25(10)ABC123",
		"{\"00\":\"952012345678912345\",\"02\":\"09520123456788\",\"37\":\"25\",\"10\":\"ABC123\"}",
		"^0095201234567891234502095201234567883725^10ABC123",
		"^00952012345678912345^0209520123456788^3725^10ABC123",
		"(00)952012345678912345(02)09520123456788(37)25(10)ABC123",
		"{\"00\":\"952012345678912345\",\"02\":\"09520123456788\",\"37\":\"25\",\"10\":\"ABC123\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/414/9520123456788",
		"^4149520123456788",
		"^4149520123456788",
		"(414)9520123456788",
		"{\"414\":\"9520123456788\"}",
		"^4149520123456788",
		"^4149520123456788",
		"(414)9520123456788",
		"{\"414\":\"9520123456788\"}");

	test_parseDLuri(ctx, true,
		"https://id.gs1.org/414/9520123456788/254/32a%2Fb",
		"^414952012345678825432a/b",
		"^4149520123456788^25432a/b",
		"(414)9520123456788(254)32a/b",
		"{\"414\":\"9520123456788\",\"254\":\"32a/b\"}",
		"^414952012345678825432a/b",
		"^4149520123456788^25432a/b",
		"(414)9520123456788(254)32a/b",
		"{\"414\":\"9520123456788\",\"254\":\"32a/b\"}");

	test_parseDLuri(ctx, true,
		"https://example.com/8004/9520614141234567?01=9520123456788",
		"^80049520614141234567^0109520123456788",
		"^80049520614141234567^0109520123456788",
		"(8004)9520614141234567(01)09520123456788",
		"{\"8004\":\"9520614141234567\",\"01\":\"09520123456788\"}",
		"^010952012345678880049520614141234567",
		"^0109520123456788^80049520614141234567",
		"(01)09520123456788(8004)9520614141234567",
		"{\"01\":\"09520123456788\",\"8004\":\"9520614141234567\"}");

	test_parseDLuri(ctx, true,
		"https://example.com/01/9520123456788/89/ABC123?99=XYZ",
		"^010952012345678889ABC123^99XYZ",
		"^0109520123456788^89ABC123^99XYZ",
		"(01)09520123456788(89)ABC123(99)XYZ",
		"{\"01\":\"09520123456788\",\"89\":\"ABC123\",\"99\":\"XYZ\"}",
		"^010952012345678889ABC123^99XYZ",
		"^0109520123456788^89ABC123^99XYZ",
		"(01)09520123456788(89)ABC123(99)XYZ",
		"{\"01\":\"09520123456788\",\"89\":\"ABC123\",\"99\":\"XYZ\"}");

	test_parseDLuri(ctx, true,
		"https://example.com/01/9520123456788?99=XYZ&89=ABC123",
		"^010952012345678899XYZ^89ABC123",
		"^0109520123456788^99XYZ^89ABC123",
		"(01)09520123456788(99)XYZ(89)ABC123",
		"{\"01\":\"09520123456788\",\"99\":\"XYZ\",\"89\":\"ABC123\"}",
		"^010952012345678899XYZ^89ABC123",
		"^0109520123456788^99XYZ^89ABC123",
		"(01)09520123456788(99)XYZ(89)ABC123",
		"{\"01\":\"09520123456788\",\"99\":\"XYZ\",\"89\":\"ABC123\"}");

	/*
	 * S4T https://youtu.be/9elyEi1PT00
	 *
	 */
	/*
	test_parseDLuri(ctx, true,
		"https://example.com/00/093123450000000012?4300=GS1+Australia&4301=Michiel+Ruighaver&4302=8+Nexus+Court&4304=Mulgrave&4306=Victoria&4307=AU&420=3170&4308=%2B61412830095&s4t",
		"^000931234500000000124300GS1+Australia^4301Michiel+Ruighaver^43028+Nexus+Court^4304Mulgrave^4306Victoria^4307AU^4203170^4308+61412830095",
		"^00093123450000000012^4300GS1+Australia^4301Michiel+Ruighaver^43028+Nexus+Court^4304Mulgrave^4306Victoria^4307AU^4203170^4308+61412830095",
		"(00)093123450000000012(4300)GS1+Australia(4301)Michiel+Ruighaver(4302)8+Nexus+Court(4304)Mulgrave(4306)Victoria(4307)AU(420)3170(4308)+61412830095",
		"{\"00\":\"093123450000000012\",\"4300\":\"GS1+Australia\",\"4301\":\"Michiel+Ruighaver\",\"4302\":\"8+Nexus+Court\",\"4304\":\"Mulgrave\",\"4306\":\"Victoria\",\"4307\":\"AU\",\"420\":\"3170\",\"4308\":\"+61412830095\"}",
		"^000931234500000000124300GS1+Australia^4301Michiel+Ruighaver^43028+Nexus+Court^4304Mulgrave^4306Victoria^4307AU^4203170^4308+61412830095",
		"^00093123450000000012^4300GS1+Australia^4301Michiel+Ruighaver^43028+Nexus+Court^4304Mulgrave^4306Victoria^4307AU^4203170^4308+61412830095",
		"(00)093123450000000012(4300)GS1+Australia(4301)Michiel+Ruighaver(4302)8+Nexus+Court(4304)Mulgrave(4306)Victoria(4307)AU(420)3170(4308)+61412830095",
		"{\"00\":\"093123450000000012\",\"4300\":\"GS1+Australia\",\"4301\":\"Michiel+Ruighaver\",\"4302\":\"8+Nexus+Court\",\"4304\":\"Mulgrave\",\"4306\":\"Victoria\",\"4307\":\"AU\",\"420\":\"3170\",\"4308\":\"+61412830095\"}");
	*/

	free(ctx);

}


static void test_URIunescape(const char *in, const char *expect_path, const char *expect_query) {

	char out[GS1_DL_MAX_AI_LEN+1];

	TEST_CHECK(URIunescape(out, GS1_DL_MAX_AI_LEN, in, strlen(in), false) == strlen(expect_path));
	TEST_CHECK(strcmp(out, expect_path) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected query component: %s", in, out, expect_path);

	TEST_CHECK(URIunescape(out, GS1_DL_MAX_AI_LEN, in, strlen(in), true) == strlen(expect_query));
	TEST_CHECK(strcmp(out, expect_query) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected path component: %s", in, out, expect_query);

}

static void test_dl_URIunescape(void) {

	char out[GS1_DL_MAX_AI_LEN+1];

	test_URIunescape("", "", "");
	test_URIunescape("test", "test", "test");
	test_URIunescape("+", "+", " ");				// "+" means space in query info
	test_URIunescape("%20", " ", " ");
	test_URIunescape("%20AB", " AB", " AB");
	test_URIunescape("A%20B", "A B", "A B");
	test_URIunescape("AB%20", "AB ", "AB ");
	test_URIunescape("ABC%2", "ABC%2", "ABC%2");			// Off end
	test_URIunescape("ABCD%", "ABCD%", "ABCD%");
	test_URIunescape("A%20%20B", "A  B",  "A  B");			// Run together
	test_URIunescape("A%01B", "A" "\x01" "B", "A" "\x01" "B");	// "Minima", we check \0 below
	test_URIunescape("A%ffB", "A" "\xFF" "B", "A" "\xFF" "B");	// Maxima
	test_URIunescape("A%FfB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%fFB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%FFB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%4FB", "AOB", "AOB");
	test_URIunescape("A%4fB", "AOB", "AOB");
	test_URIunescape("A%4gB", "A%4gB", "A%4gB");			// Non hex digit
	test_URIunescape("A%4GB", "A%4GB", "A%4GB");			// Non hex digit
	test_URIunescape("A%g4B", "A%g4B", "A%g4B");			// Non hex digit
	test_URIunescape("A%G4B", "A%G4B", "A%G4B");			// Non hex digit

	// Check that \0 is sane, although we are only working with strings
	TEST_CHECK(URIunescape(out, GS1_DL_MAX_AI_LEN, "A%00B", 5, false) == 3);
	TEST_CHECK(memcmp(out, "A" "\x00" "B", 4) == 0);

	// Truncated input
	TEST_CHECK(URIunescape(out, GS1_DL_MAX_AI_LEN, "ABCD", 2, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);				// Includes \0

	// Truncated output
	TEST_CHECK(URIunescape(out, 2, "ABCD", 4, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);				// Includes \0

	TEST_CHECK(URIunescape(out, 1, "ABCD", 4, false) == 1);
	TEST_CHECK(memcmp(out, "A", 2) == 0);				// Includes \0

	TEST_CHECK(URIunescape(out, 0, "ABCD", 4, false) == 0);
	TEST_CHECK(memcmp(out, "", 1) == 0);

}


TEST_LIST = {
	{ "dl_gs1_parseDLuri", test_dl_parseDLuri },
	{ "dl_URIunescape", test_dl_URIunescape },
	{ NULL, NULL }
};


#endif  /* UNIT_TESTS */


#ifdef FUZZER

int LLVMFuzzerTestOneInput(const char *buf, size_t len) {

	static struct gs1DLparser ctx;
	static char in[65536];

	memcpy(in, buf, len);
	in[len] = '\0';

	gs1_parseDLuri(&ctx, in);

	return 0;

}

#endif  /* FUZZER */
