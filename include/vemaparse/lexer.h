
#ifndef VEMAPARSE_LEXER_H_
#define VEMAPARSE_LEXER_H_

#include <locale>
#include <cassert>
#include <exception>
#include <set>
#include <iterator>

#if defined(_MSC_VER)
#define NOEXCEPT
#else
#define NOEXCEPT noexcept
#endif

#define NUM_ELEM(x) (sizeof(x)/sizeof(x[0]))

namespace lexer
{
enum Token
{
    IDENTIFIER = 0,
    OPERATOR,
    STRING_LITERAL,
    WHITESPACE,
    NUMBER_LITERAL,
    OPEN_BRACE,
    CLOSE_BRACE,
    OPEN_BRACKET,
    CLOSE_BRACKET,
    OPEN_PAREN,
    CLOSE_PAREN,
    NUM_TOKENS,
    INVALID = NUM_TOKENS
};

template <typename Iterator> class Lexer;

struct LexerError : public std::exception
{
    std::string message;
    LexerError(const std::string &error) : message(error) { }
    ~LexerError() NOEXCEPT { }
    const char *what() const NOEXCEPT
    {
        return message.c_str();
    }
};

template <typename Iterator>
struct LexerIterator : public std::iterator<std::forward_iterator_tag, Iterator>
{
    typedef typename Iterator::difference_type difference_type;
    const Lexer<Iterator> *lexer;
    Iterator begin, end;
    Token token;
    bool is_end;
    LexerIterator() : lexer(NULL), token(INVALID), is_end(true) { }
    LexerIterator(const Lexer<Iterator> *lexer_, Token token_, Iterator begin_, Iterator end_) : lexer(lexer_), begin(begin_), end(end_), token(token_), is_end(false) { }
    LexerIterator(const Lexer<Iterator> *lexer_, Iterator end_) : lexer(lexer_), begin(end_), end(end_), token(INVALID), is_end(true) { }

    LexerIterator &operator ++();
    LexerIterator operator ++(int);

    std::string operator *() const
    {
        if (is_end) {
            throw LexerError("dereferencing end iterator");
        }
        assert(begin != end);
        Iterator tmp = begin;
        std::vector<char> buf;
        while (tmp != end)
            buf.push_back(*tmp++);
        return std::string(buf.begin(), buf.end());
    }

    bool operator==(const LexerIterator &other)
    {
        if (this == &other)
            return true;
        if (this->is_end || other.is_end)
            return this->is_end == other.is_end;
        assert(this->lexer == other.lexer);
        return this->begin == other.begin;
    }

    bool operator!=(const LexerIterator &other)
    {
        return !(*this == other);
    }

    difference_type operator -(const LexerIterator &other)
    {
        return end - other.end;
    }
};

template <typename Iterator>
class Lexer
{
    template <typename> friend struct LexerIterator;
    Iterator begin_pos, end_pos;
    std::locale locale;
    bool skip_ws;

private:
    LexerIterator<Iterator> next(const LexerIterator<Iterator> &iter) const
    {
        if (iter.is_end)
            return iter;
        return next(iter.end);
    }

    LexerIterator<Iterator> next(const Iterator &start) const
    {
        const char specials_[] = "{}()[]#";
        const auto is_special = [&specials_](const char c) -> bool { 
            for (const char *s = specials_; *s; ++s)
                if (c == *s) 
                    return true;
            return false;
        };
        Iterator cur = start, end_pos = this->end_pos;
        if (cur == end_pos)
            return this->end();

        // space
        if (std::isspace(*cur, locale)) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && std::isspace(*cur, locale))
                ++cur;
            if (!skip_ws)
                return LexerIterator<Iterator>(this, WHITESPACE, begin_pos, cur);
        }
        if (cur == end_pos)
            return this->end();

        // scopes
        auto consume_single = [this, &cur](Token t) -> LexerIterator<Iterator> {Iterator begin_pos = cur++; return LexerIterator<Iterator>(this, t, begin_pos, cur); };
        switch (*cur) {
        case '{':
            return consume_single(OPEN_BRACE);
        case '}':
            return consume_single(CLOSE_BRACE);
        case '[':
            return consume_single(OPEN_BRACKET);
        case ']':
            return consume_single(CLOSE_BRACKET);
        case '(':
            return consume_single(OPEN_PAREN);
        case ')':
            return consume_single(CLOSE_PAREN);
        default:
            break;
        }

        // TODO: handle preprocessor directives?

        // quoted strings
        if (*cur == '"') {
            bool open_slash = false;
            Iterator begin_pos = cur++;
            while (cur != end_pos) {
                if (*cur == '"' && !open_slash)
                    break;
                if (*cur == '\\')
                    open_slash = !open_slash;
                else
                    open_slash = false;
                ++cur;
            }
            if (cur == end_pos) 
                throw LexerError("string literal not closed");
            return LexerIterator<Iterator>(this, STRING_LITERAL, begin_pos, ++cur);
        }

        // identifiers
        if (std::isalpha(*cur, locale) || (*cur == '_')) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && (std::isalnum(*cur, locale) || (*cur == '_')))
                ++cur;
            return LexerIterator<Iterator>(this, IDENTIFIER, begin_pos, cur);
        }

        // numbers - check for illegal numbers later
        if (std::isdigit(*cur, locale)) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && (std::isxdigit(*cur, locale) || (*cur == 'x') || (*cur == '.')))
                ++cur;
            return LexerIterator<Iterator>(this, NUMBER_LITERAL, begin_pos, cur);
        }

        // operators
        if (std::ispunct(*cur, locale)) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && std::ispunct(*cur, locale) && !is_special(*cur))
                ++cur;
            return LexerIterator<Iterator>(this, OPERATOR, begin_pos, cur);
        }

        throw LexerError("unknown input type");
    }

public:
    typedef LexerIterator<Iterator> iterator;
    Lexer() { }
    Lexer(Iterator begin_, Iterator end_, bool skip_ws_ = true) : begin_pos(begin_), end_pos(end_), skip_ws(skip_ws_) { }

    iterator begin() const
    {
        return this->next(begin_pos);
    }

    iterator end() const
    {
        return iterator(this, end_pos);
    }
};

template <typename Iterator>
inline LexerIterator<Iterator> &LexerIterator<Iterator>::operator ++()
{
    *this = lexer->next(this->end);
    return *this;
}

template <typename Iterator>
inline LexerIterator<Iterator> LexerIterator<Iterator>::operator ++(int)
{
    LexerIterator tmp = *this;
    *this = lexer->next(this->end);
    return tmp;
}
}



#endif

