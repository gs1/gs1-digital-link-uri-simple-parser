/**
 * GS1 Digital Link URI parser example commandline application
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
#include <stdio.h>
#include <string.h>

#include "gs1dlparser.h"

int main(int argc, char *argv[]) {

	// Responsibility of the user to ensure that buffers are adequate for
	// the application
	char in[2048];
	char out_json[GS1_DL_MAX_OUT_JSON];
	char out_brkt[GS1_DL_MAX_OUT_BRKT];
	char out_unbr[GS1_DL_MAX_OUT_UNBR];

	struct gs1DLparser ctx;

	if (argc != 2) {
		printf("Usage: %s '<Digital Link URI>'\n", argv[0]);
		printf("  Example: %s 'https://id.gs1.org/01/09520123456788/10/ABC%%2F123/21/12345?17=180426'\n", argv[0]);
		return 1;
	}

	strcpy(in, argv[1]);

	if (!gs1_parseDLuri(&ctx, in)) {
		printf("Error: %s\n", ctx.err);
		return 1;
	}

	printf("Provided Digital Link URI:                                 %s\n", in);

	gs1_writeUnbracketedAIelementString(&ctx, false, false, out_unbr);
	printf("Unbracketed element string:                                %s\n", out_unbr);

	gs1_writeUnbracketedAIelementString(&ctx, false, true, out_unbr);
	printf("Unbracketed element string (extra FNC1s):                  %s\n", out_unbr);

	gs1_writeUnbracketedAIelementString(&ctx, true, false, out_unbr);
	printf("Unbracketed element string (fixed AIs first):              %s\n", out_unbr);

	gs1_writeUnbracketedAIelementString(&ctx, true, true, out_unbr);
	printf("Unbracketed element string (fixed AIs first; extra FNC1s): %s\n", out_unbr);

	gs1_writeBracketedAIelementString(&ctx, false, out_brkt);
	printf("Bracketed element string:                                  %s\n", out_brkt);

	gs1_writeBracketedAIelementString(&ctx, true, out_brkt);
	printf("Bracketed element string (fixed AIs first):                %s\n", out_brkt);

	gs1_writeJSON(&ctx, false, out_json);
	printf("JSON:                                                      %s\n", out_json);

	gs1_writeJSON(&ctx, true, out_json);
	printf("JSON (fixed AIs first):                                    %s\n", out_json);

	return 0;

}
