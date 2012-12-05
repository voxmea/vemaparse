
#ifndef MEL_AST_H_
#define MEL_AST_H_

#include <iostream>
#include <string>
#include <functional>
#include <sstream>
#include <stdint.h>
#include <boost/xpressive/xpressive.hpp>

#include "lexer.h"
#include "parser.h"

namespace ast
{

struct Node;

struct Value
{
    enum ValueType
    {
        UINT_TYPE,
        DOUBLE_TYPE,
        FUNCTION_TYPE,
        STRING_TYPE
    };
    union
    {
        uint64_t uint_val;
        double double_val;
        Node *function_val;
        std::string *string_val;
    };
    ValueType type;
};

static std::string default_debug(std::ostream &stream, const Node *node);

struct Node
{
    std::string text;
    std::string name;
    Value value;
    std::function<std::string(std::ostream &)> debug;
    std::vector<Node *> children;

    Node() { debug = [this](std::ostream &s) {return default_debug(s, this);}; }
    Node(const std::string text_, const Value &value_) 
        : text(text_), value(value_)
    { debug = [this](std::ostream &s) {return default_debug(s, this);}; }
};

static std::string default_debug(std::ostream &stream, const Node *node)
{
    static uint64_t counter = 0;
    std::ostringstream ss;
    ss << node->name << counter++;
    std::string name = ss.str();

    if (!node->text.empty()) {
        stream << name << " [label=\"" << boost::xpressive::regex_replace(node->text, boost::xpressive::sregex::compile("\""), std::string("\\\"")) << "\"];" << std::endl;
    }

    std::vector<std::string> names;
    for (std::vector<Node *>::const_iterator iter = node->children.begin(); iter != node->children.end(); ++iter)
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
    value.type = Value::UINT_TYPE;
    if (text.size() < 3) {
        ss >> std::dec >> value.uint_val;
    } else {
        if (text[0] == '0' && text[1] == 'x') {
            ss.str(text.substr(2, text.size()));
            ss >> std::hex >> value.uint_val;
        } else if (text.find(".") != std::string::npos) {
            ss >> value.double_val;
            value.type = Value::DOUBLE_TYPE;
        } else {
            ss >> std::dec >> value.uint_val;
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
    node.value.type = Value::STRING_TYPE;
    switch (token) {
    case lexer::IDENTIFIER:
        node.value.string_val = &node.text;
        node.name = "IDENTIFIER";
        break;

    case lexer::NUMBER_LITERAL:
        node.name = "NUMBER";
        ::ast::to_number(node.text, node.value);
        break;

    case lexer::STRING_LITERAL:
    {
        node.name = "STRING";
        std::string *s = new std::string();
        *s = boost::xpressive::regex_replace(node.text, boost::xpressive::sregex::compile("(?<!\\\\)\""), std::string());
        *s = boost::xpressive::regex_replace(*s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\\""), std::string("\""));
        *s = boost::xpressive::regex_replace(*s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\n"), std::string("\n"));
        *s = boost::xpressive::regex_replace(*s, boost::xpressive::sregex::compile("(?<!\\\\)\\\\r"), std::string("\r"));
        std::cout << "got a string " << *s << std::endl;
        node.value.string_val = s;
        break;
    }

    default:
        std::cerr << "ERROR: unkown literal type\n";
        ::exit(1);
        break;
    }
}

}

#endif
