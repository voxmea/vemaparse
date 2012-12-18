
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

#ifdef _MSC_VER
#pragma warning(push, 1)
#pragma warning(disable:4702)
#endif
#include <boost/xpressive/xpressive.hpp>
#ifdef _MSC_VER
#pragma warning(default:4996)
#pragma warning(pop)
#endif

#include "lexer.h"
#include "parser.h"

namespace ast
{

struct Scope;
template <typename> struct Node;

typedef boost::variant<uint64_t, double, Scope *, std::string> Value;

template <typename Iterator>
std::string default_debug(std::ostream &stream, const Node<Iterator> &node);

template <typename Iterator>
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
    Type type;
    Iterator begin, end;
    std::string text;
    std::string name;
    Value value;
    std::function<std::string(std::ostream &)> debug;
    node_ptr parent;

    typedef typename std::list<node_ptr>::iterator child_iterator_type;
    typedef typename std::list<node_ptr>::const_iterator const_child_iterator_type;
    std::list<node_ptr> children;

    Node() : type(INVALID)
    {
        debug = [this](std::ostream &s) {return default_debug(s, *this);};
    }

    Node(const Iterator b_, const Iterator e_) 
        : type(INVALID), begin(b_), end(e_) 
    {
        debug = [this](std::ostream &s) {return default_debug(s, *this);};
    }
};

template <typename Iterator>
std::string to_string(typename Node<Iterator>::const_child_iterator_type begin, typename Node<Iterator>::const_child_iterator_type end)
{
    std::string ret;
    std::for_each(begin, end, [&ret](typename Node<Iterator>::node_ptr n) {ret += n->text;});
    return ret;
}

namespace detail
{
    template <typename Iterator>
    void print_children(Node<Iterator> &node)
    {
        std::cerr << node.name << " children : ";
        for (auto i = node.children.begin(); i != node.children.end(); ++i)
            std::cerr << " " << (*i)->text;
        std::cerr << "\n";
    }
}

template <typename Iterator>
std::string default_debug(std::ostream &stream, const Node<Iterator> &node)
{
    static uint64_t counter = 0;
    std::string name = boost::xpressive::regex_replace(node.name, boost::xpressive::sregex::compile(" |-|>|\n|\r|\\\\|\\(|\\)"), std::string("_"));
    {
        std::ostringstream ss;
        ss << name << counter++;
        name = ss.str();
    }

    std::string label;
    if (node.type == Node<Iterator>::VALUE) {
        std::ostringstream ss;
        ss << node.value;
        label = ss.str();
    } else if (!node.text.empty()) {
        label = ast::to_string<Iterator>(node.children.begin(), node.children.end());
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

template <typename Iterator>
void skip_node(Node<Iterator> &node)
{
    // Insert node's children into parent.
    typename Node<Iterator>::child_iterator_type iter;
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

template <typename Iterator>
void use_middle(Node<Iterator> &node)
{
    assert(node.children.size() == 3);
    typename Node<Iterator>::child_iterator_type iter;
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

template <typename Iterator>
void remove_terminals(Node<Iterator> &node)
{
    typename Node<Iterator>::child_iterator_type iter;
    // Insert node's right children into parent.
    for (iter = node.parent->children.begin(); iter != node.parent->children.end(); ++iter)
        if (iter->get() == &node)
            break;
    assert(iter != node.parent->children.end());
    iter = node.children.begin();
    while (iter != node.children.end()) {
        if ((*iter)->children.size() == 0) {
            node.children.erase(iter++);
        } else {
            ++iter;
        }
    }
}

template <typename Iterator>
void remove_terminals_match(Node<Iterator> &node, std::string regex_string)
{
    boost::xpressive::sregex regex = boost::xpressive::sregex::compile(regex_string);
    typename Node<Iterator>::child_iterator_type iter;
    iter = node.children.begin();
    while (iter != node.children.end()) {
        boost::xpressive::smatch what;
        bool matched = boost::xpressive::regex_match((*iter)->text, what, regex);
        if (matched) {
            node.children.erase(iter++);
        } else {
            ++iter;
        }
    }
}

inline bool to_number(const std::string &text, Value &value)
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

template <typename Match, typename Iterator>
static void literal(Match &match, Node<Iterator> &node)
{
    int token = match.begin.token;
    node.text = parser::to_string(match.begin, match.end);
    node.type = Node<Iterator>::VALUE;
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

template <typename Iterator>
void variable_declaration(Node<Iterator> &node)
{
    node.name = "variable_declaration";
    typename Node<Iterator>::child_iterator_type iter = node.children.begin();
    assert((*iter)->children.size() == 0);
    node.children.erase(iter++);
    // Skip the ID
    ++iter;
    if (iter != node.children.end()) {
        // initializer
        assert(node.children.size() == 3);
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

template <typename Iterator>
void unary_operator(Node<Iterator> &node)
{
    // Pass through
    if (node.children.size() <= 1) {
        skip_node(node);
        return;
    }

    typename Node<Iterator>::child_iterator_type iter = node.children.begin();
    node.name = op_to_name((*iter)->text);
    node.children.erase(iter);
}

template <typename Iterator>
void binary_operator(Node<Iterator> &node)
{
    // Pass through
    if (node.children.size() <= 1) {
        skip_node(node);
        return;
    }

    assert(node.children.size() > 2);

    typename Node<Iterator>::child_iterator_type iter = node.children.begin();
    ++iter;
    node.name = op_to_name((*iter)->text);
    while (iter != node.children.end()) {
        assert((*iter)->children.size() == 0);
        typename Node<Iterator>::child_iterator_type to_delete = iter++;
        node.children.erase(to_delete);
        assert(iter != node.children.end());
        ++iter;
    }
}

template <typename Iterator>
void string_expression(Node<Iterator> &node)
{
    node.name = "string expression";
    remove_terminals_match(node, ",");
}


}

#endif
