/**
 * GS1 Digital Link URI parser
 *
 * @file gs1dlparser.h
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

#ifndef GS1_DL_PARSER_H
#define GS1_DL_PARSER_H

/// \cond
#include <stdbool.h>
/// \endcond


#ifdef __cplusplus
extern "C" {
#endif


#define GS1_DL_MAX_AI_LEN	90							///< Set to maximum length of an AI value; currently X..90
#define GS1_DL_MAX_AIS		64							///< Set to maximum number of AIs in a Digital Link URI
#define GS1_DL_MAX_AI_BUF	(GS1_DL_MAX_AIS * (4 + GS1_DL_MAX_AI_LEN))		///< Capacity of the internal AI data buffer

#define GS1_DL_MAX_OUT_JSON	(GS1_DL_MAX_AIS * (4 + GS1_DL_MAX_AI_LEN + 6) + 2)	///< Maximum length for JSON output data
#define GS1_DL_MAX_OUT_UNBR	(GS1_DL_MAX_AIS * (4 + GS1_DL_MAX_AI_LEN + 1) + 1)	///< Maximum length for unbracketed AI output data
#define GS1_DL_MAX_OUT_BRKT	(GS1_DL_MAX_AIS * (4 + GS1_DL_MAX_AI_LEN*2 + 2) + 1)	///< Maximum length for bracketed AI output data; "(" escaped as "\("


/// Represents an AI element as offsets in the aiBuf field of gs1DLparser, e.g.
/// "(01)12312312312333"
struct gs1AIelement {
	const char *ai;                         ///< Pointer to offset in aiBuf representing an AI
	short ailen;                            ///< Length of the AI
	const char *value;                      ///< Pointer to offset in aiBuf representing an AI value
	short vallen;                           ///< Length of the AI's value
	bool fnc1;                              ///< Whether an FNC1 separator is required
};


/// Intermediate storage used by the parser. Passed as context to the parser
/// and AI format writers.
struct gs1DLparser {
	char aiBuf[GS1_DL_MAX_AI_BUF];			///< Opaque buffer for storing AI element string data
	struct gs1AIelement aiData[GS1_DL_MAX_AIS];	///< Extracted AI elements
	int numAIs;					///< Number of AI elements extracted from DL URI
	char err[128];					///< Error message
};


/**
 *  @brief Extract the AI data from an uncompressed Digital Link URI
 *
 *  This performs a lightweight parse, sufficient for extracting the AIs.
 *
 *  It does not validate the structure of the DL URI, nor the data relationships
 *  between the extracted AIs, nor the content of the AIs.
 *
 *  Extraction using convenience strings for GS1 keys is not supported.
 *
 *  Instances of AI (01) with values of length 8, 12 and 13 are zero-padded to
 *  14 digits to facilitate the automatic conversion of a GTIN-{8,12,13} to a
 *  GTIN-14. Other lengths are left unmodified so that this bad data is
 *  preserved for reporting.
 *
 *  @param [in,out] ctx ::gs1DLparser context
 *  @param [in] dlData The candidate Digital Link URI from which AI elements will be extracted
 *  @return true if parsing succeeded, otherwise false
 */
bool gs1_parseDLuri(struct gs1DLparser *ctx, char *dlData);


/**
 *  @brief Write the extracted AI elements as an unbracketed AI element string
 *  in which a "^" character represents FNC1, e.g. ^011231231231233398ABC^99XYZ
 *
 *  @param [in,out] ctx ::gs1DLparser context
 *  @param [in] fixedFirst If true, sort predefined fixed-length AIs ahead of the others in the output
 *  @param [in] extraFNC1 If true, emit superflous FNC1 separaters between each AI, even when not strictly required
 *  @param [out] out User-provided buffer into which the element data will be written. The buffer must be at least ::GS1_DL_MAX_OUT_UNBR bytes for general inputs.
 */
void gs1_writeUnbracketedAIelementString(struct gs1DLparser *ctx, bool fixedFirst, bool extraFNC1, char *out);


/**
 *  @brief Write the extracted AI elements as a bracketed AI element string,
 *  e.g. (01)12312312312333(98)ABC(99)XYZ
 *
 *  @param [in,out] ctx ::gs1DLparser context
 *  @param [in] fixedFirst If true, sort predefined fixed-length AIs ahead of the others in the output
 *  @param [out] out User-provided buffer into which the element data will be written. The buffer must be at least ::GS1_DL_MAX_OUT_BRKT bytes for general inputs.
 */
void gs1_writeBracketedAIelementString(struct gs1DLparser *ctx, bool fixedFirst, char *out);


/**
 *  @brief Write the extracted AI elements in a basic JSON format, e.g.
 *  {"01":"12312312312333","98":"ABC","99":"XYZ"}
 *
 *  @param [in,out] ctx ::gs1DLparser context
 *  @param [in] fixedFirst If true, sort predefined fixed-length AIs ahead of the others in the output
 *  @param [out] out User-provided buffer into which the element data will be written. The buffer must be at least ::GS1_DL_MAX_OUT_JSON bytes for general inputs
 */
void gs1_writeJSON(struct gs1DLparser *ctx, bool fixedFirst, char *out);


#ifdef __cplusplus
}
#endif


#endif /* GS1_DL_PARSER_H */

