
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>
#include <vector>
#include <list>
#include <vemaparse/lexer.h>
#include <vemaparse/parser.h>
#include <vemaparse/ast.h>

struct Node;

typedef vemalex::Lexer<std::string::iterator> Lexer;
typedef vemaparse::Match<Lexer::iterator, Node> Match;
typedef vemaparse::RuleWrapper<Lexer::iterator, Node> Rule;

struct Node
{
    typedef std::shared_ptr<Node> node_ptr;
    typedef std::list<std::shared_ptr<Node>>::iterator child_iterator_type;

    std::string name;
    std::string text;
    node_ptr parent;
    std::list<std::shared_ptr<Node>> children;
    std::string debug(std::ostream &stream)
    {
        static uint64_t counter = 0;
        std::string name = boost::xpressive::regex_replace(this->name, boost::xpressive::sregex::compile(" |-|>|\n|\r|\\\\|\\(|\\)"), std::string("_"));
        {
            std::ostringstream ss;
            ss << name << counter++;
            name = ss.str();
        }

        std::string label = this->text;
        label = boost::xpressive::regex_replace(label, boost::xpressive::sregex::compile("(?<!\\\\)\""), std::string("\\\""));
        label = boost::xpressive::regex_replace(label, boost::xpressive::sregex::compile("\n|\r"), std::string("_"));
        stream << name << " [label=\"" << this->name << " - " << label << "\"];" << std::endl;

        std::vector<std::string> names;
        for (auto iter = this->children.cbegin(); iter != this->children.cend(); ++iter)
            names.push_back((*iter)->debug(stream));
        for (auto iter = names.begin(); iter != names.end(); ++iter)
            stream << name << " -> " << (*iter) << ";" << std::endl;
        return name;
    }
};

Rule r(const std::string &regex, const std::string name = "")
{
    auto regex_helper_action = [regex](Node &n){std::cout << "regex match " << regex << " -> " << n.text << std::endl;};
    auto rule = vemaparse::regex<Lexer::iterator, Node>(regex);
    if (!name.empty())
        rule->name = name;
    rule->action = regex_helper_action;
    return rule;
}

Rule t(int id, const std::string name = "")
{
    auto rule = vemaparse::terminal<Lexer::iterator, Node>(id);
    if (!name.empty())
        rule->name = name;
    return rule;
}

template <typename Iterator, typename LexerIterator>
std::string get_line(const Iterator &begin, const Iterator &end, const LexerIterator &lex_iter)
{
    std::string line_string;
    // get the line number
    {
        int line_number = 1;
        Iterator i = begin;
        while (i != lex_iter.begin) {
            if (*i++ == '\n')
                ++line_number;
        }
        std::ostringstream ss;
        ss << line_number;
        line_string = ss.str() + ": ";
    }

    // get the line
    {
        Iterator b, e;
        b = lex_iter.begin;
        e = lex_iter.end;
        if (b == end)
            --b;
        while (b != begin && (*b != '\n'))
            --b;
        if (*b == '\n')
            ++b;
        while (e != end && (*e != '\n'))
            ++e;
        // TODO: make this faster
        std::for_each(b, e, [&line_string](char c) {line_string += c;});
    }
    return line_string;
}

void create_parse_tree(Match &match, Node::node_ptr parent)
{
    const std::string match_string = vemaparse::to_string(match);
    Node::node_ptr node = std::make_shared<Node>();
    node->name = match.name;
    node->text = match_string;
    parent->children.push_back(node);

    for (auto c = match.children.begin(); c != match.children.end(); ++c) 
        create_parse_tree(**c, node);
}

void visit_match(Match &match, Node::node_ptr parent, bool failed = false)
{
    const std::string &match_string = vemaparse::to_string(match);
    if (match_string.empty() && !failed) {
        return;
    }

    Node::node_ptr node = std::make_shared<Node>();
    node->parent = parent;
    node->name = match.name;
    node->text = match_string;
    parent->children.push_back(node);

    for (auto c = match.children.begin(); c != match.children.end(); ++c) 
        visit_match(**c, node, failed);

    if (match.action) {
        match.action(*node);
    } else {
        ast::skip_node(*node);
    }
}

Rule grammar()
{
    auto open_comment = r("/\\*.*");
    auto close_comment = r("[^\\\\]*\\*/");
    auto anything = r(".*");
    auto comment = (t(vemalex::COMMENT) | (open_comment >> (anything / close_comment)));
    comment->name = "comment";

    auto id = t(vemalex::IDENTIFIER);
    id->name = "id";

    auto semi = r(";");
    semi->name = "semi";

    auto include = r("#") >> r("include") >> (t(vemalex::STRING_LITERAL) | (r("<") >> t(vemalex::IDENTIFIER) >> r(">")));
    include->name = "include";

    auto keyword = r("int") | r("float") | r("double");
    auto declaration = keyword >> id >> (anything / semi);

    auto expression = Rule::create_empty_rule();
    auto subexpression = r("\\(") >> expression >> r("\\)");
    expression = subexpression | anything;

    return +(comment | include | declaration | expression);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "USAGE: " << argv[0] << " input_file\n";
        ::exit(1);
    }
    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "ERROR: could not open \"" << argv[1] << "\"; exiting." << std::endl;
        ::exit(1);
    }
    std::string input = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    vemalex::Lexer<std::string::iterator> lexer = vemalex::Lexer<std::string::iterator>(input.begin(), input.end());
    #if 1
    try {
        for (auto iter = lexer.begin(); iter != lexer.end(); ++iter) {
            if (iter.token != vemalex::WHITESPACE)
                std::cout << std::setw(2) << iter.token << ": " << *iter << std::endl;
        }
    } catch (const vemalex::LexerError &error) {
        std::cerr << "ERROR: " << error.what() << std::endl;
    }
    #endif

    auto start = grammar();
    auto ret = start->get_match(lexer.begin(), lexer.end());
    const bool failed = ret->end != lexer.end();

    if (failed) {
        // Walk the partial parse tree
        std::shared_ptr<Match> p, m;
        p = m = ret->children.back();
        while (!m->children.empty()) {
            p = m;
            m = m->children.back();
        }

        vemalex::Lexer<std::string::iterator>::iterator lex_iter = m->end;
        // get the line number
        std::string line_string = get_line(input.begin(), input.end(), lex_iter);
        std::cerr << "ERROR: failed to parse\n" << line_string << std::endl;
        std::cerr << "last end token " << *ret->end << std::endl;
    }

    {
        Node::node_ptr root = std::make_shared<Node>();
        create_parse_tree(*ret, root);
        std::ofstream ofs("parse.dot", std::ios::binary | std::ios::trunc);
        ofs << "digraph html {\n";
        root->debug(ofs);
        ofs << "}";
    }

    {
        Node::node_ptr root = std::make_shared<Node>();
        root->name = "root";
        std::ofstream ofs;
        ofs.open("ast.dot", std::ios::binary | std::ios::trunc);
        ofs << "digraph html {\n";
        std::for_each(ret->children.begin(), ret->children.end(),
                      [&root, failed](Match::match_shared_ptr m) {visit_match(*m, root, failed);});
        // ret.match.action(ret.match, *root);
        root->debug(ofs);
        ofs << "}";
    }
}
