/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/string.h>

StringVector
string_split(const std::string &source, const std::string &delimiter,
             size_t max_tokens, const bool keep_empty)
{
    std::vector<std::string> results;
    size_t prev = 0, next = 0;

    if (!max_tokens)
      max_tokens = results.max_size();

    while ((next = source.find(delimiter, prev)) != std::string::npos &&
           --max_tokens)
      {
        if (keep_empty || (next - prev != 0))
          results.push_back(source.substr(prev, next - prev));

        prev = next + delimiter.length();
      }

    if (prev < source.size())
      results.push_back(source.substr(prev));

    return results;
}
