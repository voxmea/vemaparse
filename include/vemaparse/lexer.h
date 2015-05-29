
#ifndef VEMAPARSE_LEXER_H_
#define VEMAPARSE_LEXER_H_

#include <cassert>
#include <cstdlib>
#include <exception>
#include <set>
#include <iterator>
#include <cstddef>
#include <ctype.h>

#ifdef HAS_IN_SITU_STRING
#include <roanoke/in-situ-string.h>
#else
#include <vector>
#endif

#if defined(_MSC_VER)
#define NOEXCEPT
#pragma warning(push)
#pragma warning(disable : 4458)
#else
#ifndef NOEXCEPT
#define NOEXCEPT noexcept
#endif
#endif

#define NUM_ELEM(x) (sizeof(x)/sizeof(x[0]))

namespace vemalex
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
    COMMENT,
    UNKNOWN,
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

namespace detail
{
    template <typename T>
    struct GetDifferenceType
    {
        typedef typename T::difference_type difference_type;
    };

    template <>
    struct GetDifferenceType<char *>
    {
        typedef std::ptrdiff_t difference_type;
    };

    template <>
    struct GetDifferenceType<const char *>
    {
        typedef std::ptrdiff_t difference_type;
    };
}

template <typename Iterator>
struct LexerIterator : public std::iterator<std::forward_iterator_tag, Iterator>
{
    // typedef typename Iterator::difference_type difference_type;
    typedef typename detail::GetDifferenceType<Iterator>::difference_type difference_type;
    const Lexer<Iterator> *lexer;
    Iterator begin, end;
    Token token;
    bool is_end;
    bool skip_nl;
    LexerIterator() : lexer(NULL), token(INVALID), is_end(true), skip_nl(true) { }
    LexerIterator(const Lexer<Iterator> *lexer_, Token token_, Iterator begin_, Iterator end_) 
        : lexer(lexer_), begin(begin_), end(end_), token(token_), is_end(false), skip_nl(lexer->skip_nl)
    { }
    LexerIterator(const Lexer<Iterator> *lexer_, Iterator end_) : lexer(lexer_), begin(end_), end(end_), token(INVALID), is_end(true), skip_nl(true) { }

    LexerIterator &operator ++();
    LexerIterator operator ++(int);

#ifdef HAS_IN_SITU_STRING
    roanoke::IS_String operator *() const
    {
        if (is_end) {
            assert(false && "dereferencing end iterator");
            std::abort();
        }
        return roanoke::IS_String(begin, end);
    }
#else
    std::string operator *() const
    {
        if (is_end) {
            assert(false && "dereferencing end iterator");
            std::abort();
        }
        assert(begin != end);
        Iterator tmp = begin;
        std::vector<char> buf;
        while (tmp != end)
            buf.push_back(*tmp++);
        return std::string(buf.begin(), buf.end());
    }
#endif

    bool operator ==(const LexerIterator &other) const
    {
        if (this == &other)
            return true;
        if (this->is_end || other.is_end)
            return this->is_end == other.is_end;
        assert(this->lexer == other.lexer);
        return this->begin == other.begin;
    }

    bool operator !=(const LexerIterator &other) const
    {
        return !(*this == other);
    }

    difference_type operator -(const LexerIterator &other) const
    {
        return end - other.end;
    }

    void start_newline()
    {
        this->skip_nl = false;
    }

    void stop_newline()
    {
        this->skip_nl = true;
    }

    bool operator <(const LexerIterator &other) const
    {
        return end < other.end;
    }
};

template <typename Iterator>
class Lexer
{
    template <typename> friend struct LexerIterator;
    Iterator begin_pos, end_pos;
    bool skip_ws;
    bool return_unknown;
    mutable bool skip_nl;

    LexerIterator<Iterator> next(const LexerIterator<Iterator> &iter) const
    {
        if (iter.is_end)
            return iter;
        return next(iter.end);
    }

    bool is_special(char c) const
    {
        static const char specials_[] = "{}()[]#";
        for (const char *s = specials_; *s; ++s)
            if (c == *s) 
                return true;
        return false;
    }

    LexerIterator<Iterator> next(const Iterator &start) const
    {
        Iterator cur = start, end_pos = this->end_pos;
        if (cur == end_pos) {
            return this->end();
        }

        // space
        if (::isspace(*cur)) {
            bool has_nl = (*cur == '\n');
            Iterator begin_pos = cur++;
            while ((cur != end_pos) && ::isspace(*cur)) {
                has_nl = has_nl || (*cur == '\n');
                ++cur;
            }
            if (!skip_ws)
                return LexerIterator<Iterator>(this, WHITESPACE, begin_pos, cur);
            if (!skip_nl && has_nl) {
                return LexerIterator<Iterator>(this, WHITESPACE, begin_pos, cur);
            }
        }
        if (cur == end_pos) {
            return this->end();
        }

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

        // single line comments
        if (*cur == '/') {
            Iterator begin_pos = cur++;
            if (*cur == '/') {
                while (cur != end_pos && *cur != '\n')
                    ++cur;
                Iterator local_end_pos = cur;
                // consume the newline, but don't include it in the token
                if (cur != local_end_pos)
                    ++cur;
                return LexerIterator<Iterator>(this, COMMENT, begin_pos, local_end_pos);
            } else {
                // Not a comment
                cur = begin_pos;
            }
        }

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
            if (cur == end_pos) {
                // std::cerr << "ERROR: string literal not close\n";
                // std::abort();
                throw LexerError("string literal not closed");
            }
            return LexerIterator<Iterator>(this, STRING_LITERAL, begin_pos, ++cur);
        }

        // identifiers
        if (::isalpha(*cur) || (*cur == '_')) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && (::isalnum(*cur) || (*cur == '_')))
                ++cur;
            return LexerIterator<Iterator>(this, IDENTIFIER, begin_pos, cur);
        }

        // numbers - check for illegal numbers later
        if (::isdigit(*cur)) {
            Iterator begin_pos = cur++;
            while ((cur != end_pos) && (isxdigit(*cur) || (*cur == 'x') || (*cur == '.')))
                ++cur;
            return LexerIterator<Iterator>(this, NUMBER_LITERAL, begin_pos, cur);
        }

        // operators
        if (::ispunct(*cur)) {
            Iterator begin_pos = cur++;
            while (cur != end_pos && ::ispunct(*cur) && !is_special(*cur))
                ++cur;
            return LexerIterator<Iterator>(this, OPERATOR, begin_pos, cur);
        }

        if (return_unknown) {
            Iterator begin_pos = cur++;
            return LexerIterator<Iterator>(this, UNKNOWN, begin_pos, cur);
        }

        throw LexerError("unknown input type");
    }

public:
    typedef LexerIterator<Iterator> iterator;
    Lexer() { }
    Lexer(Iterator begin_, Iterator end_, bool skip_ws_ = true, bool skip_nl_ = true, bool return_unknown_ = false) 
        : begin_pos(begin_), end_pos(end_), skip_ws(skip_ws_), return_unknown(return_unknown_), skip_nl(skip_nl_) { }

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
    if (this->skip_nl != this->lexer->skip_nl) {
        this->lexer->skip_nl = this->skip_nl;
    }
    *this = lexer->next(this->end);
    return *this;
}

template <typename Iterator>
inline LexerIterator<Iterator> LexerIterator<Iterator>::operator ++(int)
{
    if (this->skip_nl != this->lexer->skip_nl) {
        this->lexer->skip_nl = this->skip_nl;
    }
    LexerIterator tmp = *this;
    *this = lexer->next(this->end);
    return tmp;
}
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif


#endif

