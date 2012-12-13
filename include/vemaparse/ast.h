
#ifndef MEL_AST_H_
#define MEL_AST_H_

#include <iostream>
#include <list>
#include <deque>
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
    typedef std::list<node_ptr>::const_iterator const_child_iterator_type;
    std::list<node_ptr> children;
    Type type;

    Node() : type(INVALID) {debug = [this](std::ostream &s) {return default_debug(s, *this);};}
};

inline std::string to_string(Node::const_child_iterator_type begin, Node::const_child_iterator_type end)
{
    std::string ret;
    std::for_each(begin, end, [&ret](Node::node_ptr n) {ret += n->text;});
    return ret;
}

namespace detail
{
    inline void print_children(Node &node)
    {
        std::cerr << node.name << " children : ";
        for (auto i = node.children.begin(); i != node.children.end(); ++i)
            std::cerr << " " << (*i)->text;
        std::cerr << "\n";
    }
}

static std::string default_debug(std::ostream &stream, const Node &node)
{
    static uint64_t counter = 0;
    std::string name = boost::xpressive::regex_replace(node.name, boost::xpressive::sregex::compile(" |-|>|\n|\r|\\\\|\\(|\\)"), std::string("_"));
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
        label = ast::to_string(node.children.begin(), node.children.end());
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

static void skip_node(Node &node)
{
    // Insert node's children into parent.
    Node::child_iterator_type iter;
    for (iter = node.parent->children.begin(); iter != node.parent->children.end(); ++iter)
        if (iter->get() == &node)
            break;
    // If this node was already skipped, and we're trying to skip it again,
    // just ignore.
    if (iter == node.parent->children.end()) {
        assert(node.children.size() == 0);
        return;
    }
    // If terminal (i.e. no children), do nothing.
    if (node.children.size() == 0)
        return;
    node.parent->children.insert(iter, node.children.begin(), node.children.end());
    node.children.clear();
    node.parent->children.erase(iter);
    #if 0
    std::cerr << "Skipping " << node.name << ": " << node.text << std::endl;
    std::cerr << " parent: ";
    for (auto i = node.parent->children.begin(); i != node.parent->children.end(); ++i)
        std::cerr << ", " << (*i)->text;
    std::cerr << "\n";
    #endif
}

static void use_middle(Node &node)
{
    assert(node.children.size() == 3);
    Node::child_iterator_type iter;
    // Insert node's right children into parent.
    for (iter = node.parent->children.begin(); iter != node.parent->children.end(); ++iter)
        if (iter->get() == &node)
            break;
    assert(iter != node.parent->children.end());
    iter = node.children.begin();
    node.children.erase(iter++);
    ++iter;
    node.children.erase(iter);
    skip_node(node);
}

static void skip_left(Node &node)
{
    assert(node.children.size() >= 1);
    Node::child_iterator_type iter;
    // Insert node's right children into parent.
    for (iter = node.parent->children.begin(); iter != node.parent->children.end(); ++iter)
        if (iter->get() == &node)
            break;
    assert(iter != node.parent->children.end());
    for (auto i = node.children.begin(); i != node.children.end(); ++i)
        std::cerr << "child " << (*i)->text << std::endl;
    std::cerr << "Erasing " << ((*node.children.begin())->text) << std::endl;
    node.children.erase(node.children.begin());
    node.parent->children.insert(iter, node.children.begin(), node.children.end());
    node.parent->children.erase(iter);
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

static void variable_declaration(Node &node)
{
    node.name = "variable_declaration";
    Node::child_iterator_type iter = node.children.begin();
    assert((*iter)->children.size() == 0);
    node.children.erase(iter++);
    if (iter != node.children.end()) {
        // initializer
        assert(node.children.size() == 3);
        ++iter;
        // Get rid of equals
        node.children.erase(iter++);
    }
}

static std::string op_to_name(std::string op)
{
    if (op == "+")
        return "plus";
    else if (op == "-")
        return "minus";
    else if (op == "*")
        return "mul";
    else if (op == "/")
        return "div";
    else if (op == "&")
        return "bin_and";
    else if (op == "|")
        return "bin_or";
    else if (op == "%")
        return "mod";
    else if (op == ">>")
        return "right shift";
    else if (op == "<<")
        return "left shift";
    else if (op == "==")
        return "equals";
    else if (op == "!=")
        return "not equals";
    else if (op == "<")
        return "less than";
    else if (op == ">")
        return "greater than";
    else if (op == "<=")
        return "lte";
    else if (op == ">=")
        return "gte";
    else if (op == "&&")
        return "logical_and";
    else if (op == "||")
        return "logical_or";
    else if (op == "++")
        return "unary_plus";
    else if (op == "--")
        return "unary_minus";
    else if (op == "-")
        return "minus";
    return "I DONT KNOW " + op;
}

static void unary_operator(Node &node)
{
    // Pass through
    if (node.children.size() <= 1) {
        skip_node(node);
        return;
    }

    Node::child_iterator_type iter = node.children.begin();
    node.name = op_to_name((*iter)->text);
    node.children.erase(iter);
}

static void binary_operator(Node &node)
{
    // Pass through
    if (node.children.size() <= 1) {
        skip_node(node);
        return;
    }

    assert(node.children.size() > 2);

    Node::child_iterator_type iter = node.children.begin();
    ++iter;
    node.name = op_to_name((*iter)->text);
    while (iter != node.children.end()) {
        assert((*iter)->children.size() == 0);
        Node::child_iterator_type to_delete = iter++;
        node.children.erase(to_delete);
        assert(iter != node.children.end());
        ++iter;
    }
}


}

#endif
