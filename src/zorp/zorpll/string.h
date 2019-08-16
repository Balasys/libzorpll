/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STRING_H_INCLUDED
#define ZORP_STRING_H_INCLUDED

#include <vector>
#include <string>

/**
 * Vector of Strings
 **/
using StringVector = std::vector<std::string>;

/**
 * string_split:
 * @param source string, that should be splitted
 * @param delimiter that should separate the pieces
 * @param max_tokens limits the number of string pieces
 * @param keep_empty flag sets if empty piece should be stored or not
 * @returns Vector of strings
 *
 * Splits string input into separate strings stored in vector.
 *
 **/
StringVector
string_split(const std::string &source, const std::string &delimiter = " ",
             size_t max_tokens = 0, const bool keep_empty = false);

#endif
