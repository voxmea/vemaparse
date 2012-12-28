
vemaparse
^^^^^^^^^

Simple recursive descent parser.

Example::

  #include <vemaparse/lexer.h>
  #include <vemaparse/parser.h>
  #include <vemaparse/ast.h>
  
  using namespace ast;
  
  typedef lexer::LexerIterator<std::string::iterator> parser_iterator_type;
  typedef parser::Rule<parser_iterator_type>::Match Match;
  
  parser::Rule<parser_iterator_type> &r(const std::string &regex, const std::string name = "")
  {
      auto &rule = parser::regex<parser_iterator_type>(regex);
      if (!name.empty())
          rule.name = name;
      return rule;
  }
  
  parser::Rule<parser_iterator_type> &t(int id, const std::string name = "")
  {
      auto &rule = parser::terminal<parser_iterator_type>(id);
      if (!name.empty())
          rule.name = name;
      return rule;
  }
  
  parser::Rule<parser_iterator_type> grammar()
  {
      // comment
      auto &open_comment = r("/\\*.*");
      auto &close_comment = r("[^\\\\]*\\*/");
      auto &anything = r(".*");
      auto &comment = (open_comment >> (anything / close_comment));
      comment.name = "comment";
  
      auto &id = t(lexer::IDENTIFIER, "id");
      auto literal_action = [](Match &m, Node &n) {ast::literal(m, n);};
      auto &num = t(lexer::NUMBER_LITERAL, "num")[literal_action];
      auto &string = t(lexer::STRING_LITERAL, "string");
  
      auto &expression = *new parser::Rule<parser_iterator_type>();
  
      auto &initializer = r("=") >> expression;
      initializer.name = "initializer";
  
      // variable declaration
      auto &variable_declaration = r("var") >> id >> -initializer >> r(";");
      variable_declaration.name = "variable_declaration";
  
      // expressions
      auto expression_action = [](Match &m, Node &) {std::cout << "(" << parser::to_string(m.begin, m.end) << ")";};
      auto &primary_expression = id | num | string | (r("\\(") >> expression >> r("\\)"));
      primary_expression.name = "primary_expression";
      auto &unary_expression = ((r("\\+\\+") | r("--") | r("-") | r("\\~") | r("!")) >> primary_expression) | primary_expression;
      unary_expression.name = "unary_expression";
      auto &multiplicative_expression = unary_expression >> *((r("\\*") | r("/") | r("%")) >> unary_expression);
      multiplicative_expression.name = "multiplicative_expression";
      auto &additive_expression = multiplicative_expression >> *((r("\\+") | r("-")) >> multiplicative_expression);
      additive_expression.name = "additive_expression";
      auto &comparative_expression = additive_expression >> *(r("==") | r("!=") | r("<") | r(">") | r("<=") | r(">=") >> additive_expression);
      comparative_expression.name = "comparative_expression";
      expression = comparative_expression >> *((r("&&") | r("\\|\\|")) >> comparative_expression);
      expression.name = "expression";
      expression[expression_action];
  
      // block
      auto &statement = *new parser::Rule<parser_iterator_type>();
      statement.name = "statement";
      auto &block = r("\\{") >> *statement >> r("\\}");
      block.name = "block";
      statement = block | variable_declaration | comment;
  
      return *block;
  }
  
  void visit_match(Match &match, Node *parent)
  {
      static const std::bitset<lexer::NUM_TOKENS> literals((1 << lexer::IDENTIFIER) | 
                                                           (1 << lexer::STRING_LITERAL) | 
                                                           (1 << lexer::NUMBER_LITERAL));
      Node *node = parent;
      const std::string match_string = parser::to_string(match.begin, match.end);
      if (match_string.empty()) {
          return;
      }
      if (match.action || parent->text != match_string) {
          node = new Node();
          node->name = boost::xpressive::regex_replace(match.rule.name, boost::xpressive::sregex::compile(" |-|>"), std::string("_"));
          node->text = match_string;
          parent->children.push_back(node);
      }
      if (match.action)
          match.action(match, *node);
      for (auto c = match.children.begin(); c != match.children.end(); ++c) 
          visit_match(**c, node);
  }
  
  int main(int argc, char *argv[])
  {
      std::string input;
      lexer::Lexer<std::string::iterator> lexer;
      if (argc > 1) {
          std::ifstream file(argv[1]);
          input = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
          lexer = lexer::Lexer<std::string::iterator>(input.begin(), input.end());
  #if 0
          try {
              for (auto iter = lexer.begin(); iter != lexer.end(); ++iter) {
                  if (iter.token != lexer::WHITESPACE)
                      std::cout << std::setw(2) << iter.token << ": " << *iter << std::endl;
              }
          } catch (const lexer::LexerError<std::string::iterator> &error) {
              std::cerr << "ERROR: " << error.what() << std::endl;
          }
  #endif
      } else {
          std::getline(std::cin, input);
          if (!input.empty()) {
              lexer = lexer::Lexer<std::string::iterator>(input.begin(), input.end());
          }
      }
  
      parser::Rule<parser_iterator_type> start = grammar();
      Node *root = new Node();
      parser::Rule<parser_iterator_type>::rule_result ret = start.match(lexer.begin(), lexer.end());
  
      {
          root->name = "root";
          std::ofstream ofs("ast.dot");
          ofs << "digraph html {\n";
          std::for_each(ret.match.children.begin(), ret.match.children.end(),
                        [root](Match::match_ptr m) {visit_match(*m, root);});
          std::cout << "\n\n";
          root->debug(ofs);
          ofs << "}";
      }
  
      if (ret.match.end != lexer.end()) {
          // Walk the partial parse tree
          std::shared_ptr<Match> p, m;
          p = m = ret.match.children.back();
          while (!m->children.empty()) {
              p = m;
              m = m->children.back();
          }
  
          lexer::Lexer<std::string::iterator>::iterator lex_iter = m->end;
  
          // get the line number
          std::string line_string;
          {
              int line_number = 1;
              auto i = input.begin();
              while (i != lex_iter.begin) {
                  if (*i++ == '\n')
                      ++line_number;
              }
              std::ostringstream ss;
              ss << line_number;
              line_string = ss.str() + ": ";
          }
  
          // Grab the line
          std::string::iterator begin, end;
          begin = lex_iter.begin;
          end = lex_iter.end;
          if (begin == input.end())
              --begin;
          while (begin != input.begin() && (*begin != '\n'))
              --begin;
          if (*begin == '\n')
              ++begin;
          while (end != input.end() && (*end != '\n'))
              ++end;
          std::cout << "Failed to parse: \n" << line_string;
          std::for_each(begin, end, [](char c) {std::cout << c;});
          std::cout << "\n";
          for (int i = 0; i < ((lex_iter.begin - begin) + line_string.size()); ++i)
              std::cout << " ";
          std::cout << "^\n";
          std::cout << "Tried rules: " << p->rule.name;
          for (auto i = p->children.begin(); i != p->children.end(); ++i)
              std::cout << ", " << (*i)->rule.name;
          std::cout << "\n";
      }
  }
  
