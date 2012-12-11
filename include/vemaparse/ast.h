
#ifndef MEL_AST_H_
#define MEL_AST_H_

#include <iostream>
#include <string>
#include <functional>
#include <sstream>
#include <stdint.h>
#include <boost/xpressive/xpressive.hpp>
#include <boost/variant.hpp>

#include "lexer.h"
#include "parser.h"

namespace ast
{

struct Scope;

typedef boost::variant<uint64_t, double, Scope *, std::string> Value;

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
        std::string text = boost::xpressive::regex_replace(node->text, boost::xpressive::sregex::compile("(?<!\\\\)\""), std::string("\\\""));
        stream << name << " [label=\"" << text << "\"];" << std::endl;
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
