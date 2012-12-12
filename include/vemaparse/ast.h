
#ifndef MEL_AST_H_
#define MEL_AST_H_

#include <iostream>
#include <list>
#include <string>
#include <functional>
#include <memory>
#include <sstream>
#include <stdint.h>
#include <boost/xpressive/xpressive.hpp>
#include <boost/variant.hpp>

#include "lexer.h"
#include "parser.h"

namespace ast
{

struct Scope;
struct Node;

typedef boost::variant<uint64_t, double, Scope *, std::string> Value;

static std::string default_debug(std::ostream &stream, const Node &node);

struct Node
{
    typedef std::shared_ptr<Node> node_ptr;
    enum Type
    {
        VALUE,
        ASSIGNMENT,
        EXPRESSION,
        STRING_EXPRESSION,
        NUM_TYPES,
        INVALID = NUM_TYPES
    };
    std::string text;
    std::string name;
    Value value;
    std::function<std::string(std::ostream &)> debug;
    node_ptr parent;
    typedef std::list<node_ptr>::iterator child_iterator_type;
    std::list<node_ptr> children;
    Type type;

    Node() : type(INVALID) {debug = [this](std::ostream &s) {return default_debug(s, *this);};}
};

static std::string default_debug(std::ostream &stream, const Node &node)
{
    static uint64_t counter = 0;
    std::string name = boost::xpressive::regex_replace(node.name, boost::xpressive::sregex::compile(" |-|>|\n|\r|\\\\"), std::string("_"));
    {
        std::ostringstream ss;
        ss << name << counter++;
        name = ss.str();
    }

    std::string label;
    if (node.type == Node::VALUE) {
        std::ostringstream ss;
        ss << node.value;
        label = ss.str();
    } else if (!node.text.empty()) {
        label = node.text;
    }
    label = boost::xpressive::regex_replace(label, boost::xpressive::sregex::compile("(?<!\\\\)\""), std::string("\\\""));
    label = boost::xpressive::regex_replace(label, boost::xpressive::sregex::compile("\n|\r"), std::string("_"));
    stream << name << " [label=\"" << node.name << " - " << label << "\"];" << std::endl;

    std::vector<std::string> names;
    for (auto iter = node.children.cbegin(); iter != node.children.cend(); ++iter)
        names.push_back((*iter)->debug(stream));
    for (auto iter = names.begin(); iter != names.end(); ++iter)
        stream << name << " -> " << (*iter) << ";" << std::endl;
    return name;
}

static bool to_number(const std::string &text, Value &value)
{
    if (text.empty())
        return 0;
    std::istringstream ss(text);
    if (text.size() < 3) {
        uint64_t tmp;
        ss >> std::dec >> tmp;
        value = tmp;
    } else {
        if (text[0] == '0' && text[1] == 'x') {
            ss.str(text.substr(2, text.size()));
            uint64_t tmp;
            ss >> std::hex >> tmp;
            value = tmp;
        } else if (text.find(".") != std::string::npos) {
            double tmp;
            ss >> tmp;
            value = tmp;
        } else {
            uint64_t tmp;
            ss >> std::dec >> tmp;
            value = tmp;
        }
    }
    char c;
    if (ss.fail() || ss.get(c))
        return false;
    return true;
}

template <typename Match>
static void literal(Match &match, Node &node)
{
    int token = match.begin.token;
    node.text = parser::to_string(match.begin, match.end);
    node.type = Node::VALUE;
    switch (token) {
    case lexer::IDENTIFIER:
        node.value = node.text;
        node.name = "IDENTIFIER";
        break;

    case lexer::NUMBER_LITERAL:
        node.name = "NUMBER";
        ::ast::to_number(node.text, node.value);
        break;

    case lexer::STRING_LITERAL:
    {
        node.name = "STRING";
        std::string s = boost::xpressive::regex_replace(node.text, boost::xpressive::sregex::compile("(?<!\\\\)\""), std::string());
        s = boost::xpressive::regex_replace(s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\\""), std::string("\""));
        s = boost::xpressive::regex_replace(s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\n"), std::string("\n"));
        s = boost::xpressive::regex_replace(s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\r"), std::string("\r"));
        node.value = s;
        node.children.clear();
        break;
    }

    default:
        std::cerr << "ERROR: unkown literal type\n";
        ::exit(1);
        break;
    }
    assert(!node.children.size());
}

static void skip_node(Node &node)
{
    // Insert node's children into parent.
    Node::child_iterator_type iter;
    for (iter = node.parent->children.begin(); iter != node.parent->children.end(); ++iter)
        if (iter->get() == &node)
            break;
    // If this node was already skipped, and we're trying to skip it again,
    // just ignore.
    if (iter == node.parent->children.end())
        return;
    node.parent->children.insert(iter, node.children.begin(), node.children.end());
    node.parent->children.erase(iter);
}

}

#endif
