/**
 * @file Prompt.hpp
 * @brief Interactive terminal prompts for user input.
 */

#ifndef XI_TERMINAL_PROMPT_HPP
#define XI_TERMINAL_PROMPT_HPP

#include "../Collection/String.hpp"
#include "../Collection/Array.hpp"

using namespace Xi;
using namespace Collection;

namespace Terminal {
namespace Prompt {

/**
 * @brief Asks a question and waits for a string response.
 * @param question The prompt message.
 * @param defaultVal Optional default if user presses enter.
 * @return The user's input.
 */
String ask(const String &question, const String &defaultVal = "");

/**
 * @brief Asks for a password (hides input).
 */
String password(const String &question);

/**
 * @brief Asks a yes/no question.
 * @return true for yes, false for no.
 */
bool confirm(const String &question, bool defaultVal = true);

/**
 * @brief Presents a list of options and asks the user to select one.
 * @return The index of the selected option.
 */
usz select(const String &question, const Array<String> &options);

/**
 * @brief Presents a list of options for multiple selection.
 */
Array<usz> multiSelect(const String &question, const Array<String> &options);

} // namespace Prompt
} // namespace Terminal

#endif // XI_TERMINAL_PROMPT_HPP
