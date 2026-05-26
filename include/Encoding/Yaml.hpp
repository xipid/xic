/**
 * @file Yaml.hpp
 * @brief High-performance YAML and JSON parser and emitter for the Xi
 * framework.

 */

#ifndef XI_DATA_YAML_HPP
#define XI_DATA_YAML_HPP

#include "../Collection/Tree.hpp"

/**
 * @namespace Encoding
 * @brief Contains data serialization and processing utilities.
 */
namespace Encoding {

using namespace Xi;
using namespace Collection;

/**
 * @class YAML
 * @brief Provides static methods for YAML and JSON parsing and serialization.
 *
 * Implements a unified parser that handles YAML 1.2, JSON flow styles,
 * and multiple comment formats (//, / * * /, #). Supports anchors and aliases.
 */
class XI_EXPORT YAML {
public:
  /**
   * @brief Parses a YAML or JSON string into a TreeItem.
   * @param yamlString The input string.
   * @param outRoot The root TreeItem to populate.
   * @return True if successful, false otherwise.
   */
  static bool parse(const String &yamlString, TreeItem &outRoot);

  /**
   * @brief Hydrates a tree by checking for _type tags and instantiating custom
   * classes. Internal recursive helper.
   */
  template <typename BaseT> static void hydrateRecursive(TreeItem *item) {
    if (!item)
      return;

    if (TreeBranch *branch = dynamic_cast<TreeBranch *>(item)) {
      for (usz i = 0; i < branch->size(); ++i) {
        TreeItem *child = (*branch)[i];
        if (TaggedTreeBranch *tb = dynamic_cast<TaggedTreeBranch *>(child)) {
          TreeItem *rawType = tb->get("_type");
          if (rawType) {
            String typeName;
            if (auto s = dynamic_cast<TreeItemT<String> *>(rawType))
              typeName = s->value;

            if (typeName == demangle_type_name<BaseT>()) {
              BaseT *obj = new BaseT();
              obj->name = tb->name;
              // Preserve existing classes (set in constructor) and append new
              // ones from YAML
              for (usz c = 0; c < tb->classes.size(); ++c) {
                obj->addClass(tb->classes[c].c_str());
              }

              // Move (or clone) children from the old node to the new hydrated
              // node
              for (usz k = 0; k < tb->size(); ++k) {
                obj->add((*tb)[k]->clone());
              }

              // Replace the child in-place by treating the branch as an
              // Array<TreeItem*>
              (*branch)[i] = obj;
              obj->parent = branch;
              delete child;
              child = obj;

              if constexpr (HasParseHydrate<BaseT>::value) {
                obj->parseHydrate();
              }
            }
          }
        }
        hydrateRecursive<BaseT>(child);
      }
    }
  }

  /**
   * @brief Serializes a TreeItem into a YAML string.
   * @param root The root node.
   * @param indentation Indentation size in spaces.
   * @return The YAML string.
   */
  static String toYAML(const TreeItem &root, int indentation = 2);

  /**
   * @brief Serializes a TreeItem into a JSON string.
   * @param root The root node.
   * @param indentation Indentation size in spaces.
   * @return The JSON string.
   */
  static String toJSON(const TreeItem &root, int indentation = 4);
};

/**
 * @brief Helper function for parsing YAML or JSON.
 */
inline bool parseYAML(const String &yamlString, TreeItem &tree) {
  return YAML::parse(yamlString, tree);
}

/**
 * @brief Template helper for parsing and hydrating specific types.
 */
template <typename T>
inline bool parseYAML(const String &yamlString, TreeItem &tree) {
  if (YAML::parse(yamlString, tree)) {
    YAML::hydrateRecursive<T>(&tree);
    return true;
  }
  return false;
}

/**
 * @brief Helper function for parsing JSON (alias for YAML parser).
 */
inline bool parseJSON(const String &jsonString, TreeItem &tree) {
  return YAML::parse(jsonString, tree);
}

/**
 * @brief Template helper for parsing and hydrating specific types from JSON.
 */
template <typename T>
inline bool parseJSON(const String &jsonString, TreeItem &tree) {
  if (YAML::parse(jsonString, tree)) {
    YAML::hydrateRecursive<T>(&tree);
    return true;
  }
  return false;
}

/**
 * @brief Helper function for serializing to YAML.
 */
inline String toYAML(const TreeItem &tree, int indentation = 2) {
  return YAML::toYAML(tree, indentation);
}

/**
 * @brief Helper function for serializing to JSON.
 */
inline String toJSON(const TreeItem &tree, int indentation = 4) {
  return YAML::toJSON(tree, indentation);
}

} // namespace Encoding

#endif // XI_DATA_YAML_HPP
