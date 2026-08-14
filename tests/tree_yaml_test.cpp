#include <Collection/Tree.hpp>
#include <Encoding/Yaml.hpp>
#include <cstdio>
#include <cassert>

using namespace Collection;
using namespace Encoding;

int main() {
    std::printf("=== Running Tree & YAML Refactor Tests ===\n");

    // 1. Verify Node creation and tree building
    NodeBase* root = new Node<>();
    
    NamedNode<>* child1 = new NamedNode<>();
    child1->name = "button";
    child1->addClass("btn");
    child1->addClass("primary");

    NamedNode<int>* child2 = new NamedNode<int>(42);
    child2->setName("count");

    NamedNode<bool>* child3 = new NamedNode<bool>(true);
    child3->setName("active");
    child3->addClass("state");

    root->add(child1);
    root->add(child2);
    root->add(child3);

    // Verify basic hierarchy
    assert(root->size() == 3);
    assert(root->get("button") == child1);
    assert(root->get("count") == child2);
    assert(root->get("active") == child3);

    // 2. Verify Query selector matching
    // Test member query
    Array<NodeBase*> queryResults = root->query("button.btn");
    assert(queryResults.length() == 1);
    assert(queryResults[0] == child1);

    // Test member query matching 'this' (new requirement: "query() which calls the query with first value = this")
    assert(child1->query("button").length() == 1);
    assert(child1->query("button")[0] == child1);

    // Test global query and queryAll
    NodeBase* foundNode = query(root, "active.state");
    assert(foundNode == child3);

    Array<NodeBase*> allStates = queryAll(root, ".state");
    assert(allStates.length() == 1);
    assert(allStates[0] == child3);

    // 3. Verify YAML parsing and serialization with Node
    String yamlInput = 
        "app:\n"
        "  name: MyApp\n"
        "  debug: true\n"
        "  threads: 8\n"
        "  servers:\n"
        "    - 192.168.1.1\n"
        "    - 192.168.1.2\n";

    Node<> yamlTree;
    bool parsed = parseYAML(yamlInput, yamlTree);
    assert(parsed);
    std::printf("yamlTree size: %zu\n", yamlTree.size());
    for(size_t i = 0; i < yamlTree.size(); i++) {
        std::printf("  child %zu: name='%s'\n", i, yamlTree[i]->getName().c_str());
    }

    // Assert that we parse correctly using the generic Node classes
    NodeBase* appBranch = yamlTree.get("app");
    assert(appBranch != nullptr);
    std::printf("appBranch size: %zu\n", appBranch->size());
    for(size_t i = 0; i < appBranch->size(); i++) {
        std::printf("  appChild %zu: name='%s'\n", i, (*appBranch)[i]->getName().c_str());
    }

    NodeBase* nameNode = appBranch->get("name");
    assert(nameNode != nullptr);
    Node<String>* nameStr = dynamic_cast<Node<String>*>(nameNode);
    assert(nameStr != nullptr);
    assert(nameStr->value == "MyApp");

    NodeBase* debugNode = appBranch->get("debug");
    assert(debugNode != nullptr);
    Node<bool>* debugBool = dynamic_cast<Node<bool>*>(debugNode);
    assert(debugBool != nullptr);
    assert(debugBool->value == true);

    NodeBase* threadsNode = appBranch->get("threads");
    assert(threadsNode != nullptr);
    Node<long long>* threadsInt = dynamic_cast<Node<long long>*>(threadsNode);
    assert(threadsInt != nullptr);
    assert(threadsInt->value == 8);

    // Verify serialization back to YAML
    String serialized = toYAML(yamlTree);
    assert(serialized.length() > 0);
    std::printf("Serialized tree to YAML:\n%s", serialized.c_str());

    delete root;
    std::printf("=== All Tree & YAML Refactor Tests Passed! ===\n");
    return 0;
}
