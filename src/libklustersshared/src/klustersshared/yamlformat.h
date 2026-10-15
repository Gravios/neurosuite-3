/***************************************************************************
 * yamlformat.h  -  write a YAML document in the layout of the file it came from
 *
 * yaml-cpp keeps a document's content but not its text: comments are not
 * part of the node tree, and an emitter configured for block style expands
 * every inline list.  A parameter file is also read and hand-edited by
 * people, so a save that turns
 *
 *     nChannels: 64          # 4 shanks x 16
 *     channels: [0, 1, 2, 3]
 *     overwrite: 'false'
 *
 * into
 *
 *     nChannels: 64
 *     channels:
 *       - 0
 *       ...
 *     overwrite: false
 *
 * loses the notes, quadruples the file, and turns a string into a boolean
 * for PyYAML readers.  emitWithLayout() writes the node tree in block style
 * but takes, from the text the document was loaded from:
 *
 *   - comment and blank lines, re-emitted verbatim before the key or list
 *     item they preceded (a key is identified by its path; a list item by
 *     its `name:` when that is its first key, else by its position), plus
 *     the file's header and trailer comments;
 *   - end-of-line comments, with their original spacing;
 *   - flow style ([...] / {...}) of collections that were written inline;
 *   - quoting: a scalar that was quoted stays quoted (with the same quote
 *     character), also when the caller replaced it with a new value of the
 *     same kind;
 *   - an empty value (`date:`) instead of `~`.
 *
 * Collections the caller created get a fixed style: a list of scalars is
 * written inline, everything else in block style; a new string that would
 * read back as a boolean or null ('false', 'yes', '~') is quoted.
 *
 * Without source text the same rules give a clean block document.  The
 * result always loads to the same tree as @p doc (tested); only text the
 * tree cannot carry is borrowed from @p sourceText.
 *
 * Qt-free (yaml-cpp only), shared by ParameterYamlWriter (ndmanager) and
 * ParameterYamlModifier (NeuroScope, Klusters).
 ***************************************************************************/
#ifndef KLUSTERSSHARED_YAMLFORMAT_H
#define KLUSTERSSHARED_YAMLFORMAT_H

#include <string>
#include <yaml-cpp/yaml.h>

#include "libklustersshared_export.h"

namespace yamlformat {

/** Emits @p doc in block style, borrowing comments, flow style and quoting
 *  from @p sourceText (the text @p doc was loaded from; may be empty).
 *  Ends with a newline.  Throws what yaml-cpp throws for an unemittable
 *  node (never for a tree that came from YAML::Load). */
KLUSTERSSHARED_EXPORT std::string emitWithLayout(const YAML::Node& doc,
                                                 const std::string& sourceText);

/** Reads a whole file into a string; empty on failure. */
KLUSTERSSHARED_EXPORT std::string readText(const std::string& path);

} // namespace yamlformat

#endif // KLUSTERSSHARED_YAMLFORMAT_H
