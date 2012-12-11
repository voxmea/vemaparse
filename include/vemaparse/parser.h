
#ifndef MEL_PARSER_H_
#define MEL_PARSER_H_

#include <tuple>
#include <functional>
#include <vector>
#include <list>
#include <map>
#include <deque>
#include <algorithm>
#include <memory>

#ifdef _MSC_VER
#pragma warning(push, 1)
#pragma warning(disable:4702)
#endif
#include <boost/xpressive/xpressive.hpp>
#ifdef _MSC_VER
#pragma warning(default:4996)
#pragma warning(pop)
#endif

namespace ast
{
    struct Node;
}

namespace parser
{
template<typename T>
inline std::string to_string(T begin, T end)
{
    std::string ret;
    std::for_each(begin, end, [&ret](const std::string &s) {ret += s;});
    return ret;
}

template <typename Iterator, typename Action> struct Rule;
template <typename Iterator, typename Action> struct RuleResult;

template <typename Iterator, typename Action>
struct RuleResult
{
    struct Match
    {
        typedef std::shared_ptr<Match> match_ptr;
        Iterator begin, end;
        std::function<Action> action;
        std::deque<match_ptr> children;
    };
    bool matched;
    Match match;

    RuleResult() : matched(false) { }

    RuleResult(bool m_, Iterator e_)
        : matched(m_) 
    {
        match.end = e_;
        // Expected that this will be filled in later by the rule.
        match.begin = e_;
    }

    RuleResult(Iterator b_, Iterator e_, const Rule<Iterator, Action> &r_, std::function<void(Match &)> a_)
        : matched(true)
    {
        match.begin = b_;
        match.end = e_;
        match.rule = r_;
        match.action = a_;
    }

    RuleResult(bool m_, Iterator b_, Iterator e_, const Rule<Iterator, Action> &r_, std::function<void(Match &)> a_)
        : matched(m_)
    {
        match.begin = b_;
        match.end = e_;
        match.rule = r_;
        match.action = a_;
    }

};

template <typename Iterator, typename Action>
struct Rule
{
    typedef Match_<Iterator, Action> Match;
    typedef RuleResult<Iterator, Action> rule_result;

    Rule() : name("UNKNOWN"), must_consume_token(true), left(nullptr), right(nullptr) { }
    Rule(const std::string &name_) : name(name_), must_consume_token(true), left(nullptr), right(nullptr) { }
    Rule(const std::string &name_, Rule *l_, Rule *r_ = nullptr) : name(name_), must_consume_token(true), left(l_), right(r_) { }

    std::string name;
    bool must_consume_token;
    Rule *left, *right;
    std::function<Action> action;
    std::function<bool(Match &)> check;
    std::function<rule_result(Iterator, Iterator)> match_;

    // Match behavior:
    //  1. Not matching == returning false. end iterator must be == to begin.
    //  2. Even on fail, append any children that matched for debugging.
    rule_result match(Iterator token_pos, Iterator eos) const
    {
        if (must_consume_token && token_pos == eos)
            return rule_result(false, eos);
        rule_result ret;
        try {
            ret = match_(token_pos, eos);
        } catch (const lexer::LexerError &ex) {
            std::cerr << "ERROR: " << ex.what() << std::endl;
            ret.matched = false;
            ret.match.end = token_pos;
            return ret;
        }
        assert(ret.matched || ret.match.end == token_pos);
        ret.match.begin = token_pos;
        ret.match.rule = *this;
        ret.match.action = action;
        if (check) {
            ret.matched = check(ret.match);
            if (!ret.matched)
                ret.match.end = token_pos;
        }
        return ret;
    }

    void debug(const std::string &indent, std::set<const Rule *>visited = std::set<const Rule *>()) const
    {
        if (name == "UNKNOWN" || !match_) {
            std::cerr << "ERROR: recursive rule didn't recurse\n";
            ::exit(0);
        }
        if (left && visited.find(left) == visited.end())
            left->debug(indent + "   ", visited);
        visited.insert(this);
        std::cout << indent << name << std::endl;
        if (right && visited.find(right) == visited.end())
            right->debug(indent + "   ", visited);
    }

    Rule &operator [](std::function<Action> callable)
    {
        action = callable;
        return *this;
    }

    Rule &operator ()(std::function<bool(Match &)> callable)
    {
        check = callable;
        return *this;
    }
};

template <typename Iterator, typename Action>
Rule<Iterator, Action> &regex(const std::string &regex_string)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("regex");
    boost::xpressive::sregex regex = boost::xpressive::sregex::compile(regex_string);
    rule.match_ = [&rule, regex, regex_string](Iterator token_pos, Iterator) -> typename Rule<Iterator, Action>::rule_result { 
        boost::xpressive::smatch what;
        std::string token_string = *token_pos;
        bool matched = boost::xpressive::regex_match(token_string, what, regex);
        return typename Rule<Iterator, Action>::rule_result(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

template <typename Iterator, typename Action>
Rule<Iterator, Action> &terminal(int id)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("terminal");
    rule.match_ = [&rule, id](Iterator token_pos, Iterator) -> typename Rule<Iterator, Action>::rule_result {
        bool matched = (token_pos.token == id);
        return typename Rule<Iterator, Action>::rule_result(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

// non-terminals propagate info from their children
template <typename Iterator, typename Action>
void propagate_child_info(RuleResult<Iterator, Action> &ret, const RuleResult<Iterator, Action> &child)
{
    ret.matched = child.matched;
    ret.match.end = child.match.end;
    ret.match.children.push_back(std::make_shared<Match_<Iterator, Action>>(child.match));
}

// Ordering this >> that
template <typename Iterator, typename Action>
Rule<Iterator, Action> &operator >>(Rule<Iterator, Action> &first, Rule<Iterator, Action> &second)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("order", &first, &second);
    rule.must_consume_token = first.must_consume_token || second.must_consume_token;
    rule.match_ = [&rule](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, Action>::rule_result 
    {
        typename Rule<Iterator, Action>::rule_result ret, tmp;
        ret = rule.left->match(token_pos, eos);
        if (ret.matched) {
            tmp = rule.right->match(ret.match.end, eos);
            propagate_child_info(ret, tmp);
            if (!tmp.matched) {
                ret.match.end = token_pos;
            }
        }
        return ret;
    };
    return rule;
}

// Select this | that
template <typename Iterator, typename Action>
Rule<Iterator, Action> &operator |(Rule<Iterator, Action> &first, Rule<Iterator, Action> &second)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("or", &first, &second);
    rule.must_consume_token = first.must_consume_token || second.must_consume_token;
    rule.match_ = [&rule](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, Action>::rule_result 
    { 
        typename Rule<Iterator, Action>::rule_result ret, tmp;
        tmp = rule.left->match(token_pos, eos);
        // TODO: if both fail should we propagate all the child info? 
        // Just the failure with the most children?
        if (tmp.matched) {
            propagate_child_info(ret, tmp);
            return ret;
        }
        tmp = rule.right->match(token_pos, eos);
        propagate_child_info(ret, tmp);
        return ret;
    };
    return rule;
}

// Kleene Star
template <typename Iterator, typename Action>
Rule<Iterator, Action> &operator *(Rule<Iterator, Action> &first)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>(std::string("kleene->")+first.name, &first);
    rule.must_consume_token = false;
    rule.match_ = [&rule](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, Action>::rule_result 
    {
        typename Rule<Iterator, Action>::rule_result ret, tmp;
        ret.match.end = tmp.match.end = token_pos;
        tmp.matched = true;
        while (tmp.match.end != eos && tmp.matched) {
            tmp = rule.left->match(tmp.match.end, eos);
            propagate_child_info(ret, tmp);
        }
        ret.matched = true;
        return ret;
    };
    return rule;
}

// Non-greedy kleene star
template <typename Iterator, typename Action>
Rule<Iterator, Action> &operator /(Rule<Iterator, Action> &first, Rule<Iterator, Action> &second)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("non-greedy kleene", &first, &second);
    rule.must_consume_token = first.must_consume_token || second.must_consume_token;
    rule.match_ = [&rule](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, Action>::rule_result 
    {
        typename Rule<Iterator, Action>::rule_result ret, tmp;
        ret.matched = true;
        bool matched_right_side = false;
        tmp.match.end = token_pos;
        while (tmp.match.end != eos) {
            Iterator start_pos = tmp.match.end;
            tmp = rule.right->match(start_pos, eos);
            if (tmp.matched) {
                propagate_child_info(ret, tmp);
                matched_right_side = true;
                break;
            }
            tmp = rule.left->match(start_pos, eos);
            propagate_child_info(ret, tmp);
            if (!tmp.matched) {
                ret.match.end = token_pos;
                break;
            }
        }
        if (!matched_right_side) {
            ret.matched = false;
            ret.match.end = token_pos;
        }
        return ret;
    };
    return rule;
}

// Optional this?
template <typename Iterator, typename Action>
Rule<Iterator, Action> &operator -(Rule<Iterator, Action> &first)
{
    Rule<Iterator, Action> &rule = *new Rule<Iterator, Action>("optional", &first);
    rule.must_consume_token = false;
    rule.match_ = [&rule](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, Action>::rule_result 
    {
        if (token_pos == eos)
            return typename Rule<Iterator, Action>::rule_result(true, eos);
        typename Rule<Iterator, Action>::rule_result ret, tmp = rule.left->match(token_pos, eos);
        propagate_child_info(ret, tmp);
        if (!ret.matched)
            ret.match.end = token_pos;
        ret.matched = true;
        return ret;
    };
    return rule;
}

} // namespace parser


#endif
