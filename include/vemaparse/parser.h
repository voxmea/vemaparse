
#ifndef VEMAPARSE_PARSER_H_
#define VEMAPARSE_PARSER_H_

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

namespace vemaparse
{

template <typename Iterator, typename ActionType>
struct Match : std::enable_shared_from_this<Match<Iterator, ActionType>>
{
    bool matched;
    std::string name;
    Iterator begin, end;
    std::function<void(ActionType &)> action;
    std::deque<std::shared_ptr<Match>> children;

    Match(Iterator end_) : matched(false), end(end_) { }
    Match(bool matched_, Iterator end_) : matched(matched_), end(end_) { }

    std::shared_ptr<Match> get_shared_ptr() {return this->shared_from_this();}
};

template <typename Iterator, typename ActionType>
std::string to_string(const Match<Iterator, ActionType> &m)
{
    std::string ret;
    std::for_each(m.begin, m.end, [&ret](const std::string &s) {ret += s;});
    return ret;
}

template <typename Iterator, typename ActionType> class RuleResult;

template <typename Iterator, typename ActionType>
struct Rule : std::enable_shared_from_this<Rule<Iterator, ActionType>>
{
    typedef Match<Iterator, ActionType> match_type;
    typedef std::shared_ptr<match_type> rule_result;
    typedef void action_type(ActionType &);
    typedef bool check_type(const match_type &);

    std::string name;
    std::function<action_type> action;
    std::function<check_type> check;
    std::function<rule_result(Iterator, Iterator)> match;
    bool must_consume_token;

    Rule() { }
    Rule(const std::string name_) : name(name_) { }

    rule_result get_match(Iterator token_pos, Iterator eos) const
    {
        if (must_consume_token && token_pos == eos)
            return std::make_shared<match_type>(eos);
        rule_result ret;
        try {
            // static int depth = 0;
            // std::cout << depth++ << ":trying " << name << " on \"" << *token_pos << "\"" << std::endl;
            ret = match(token_pos, eos);
            // depth--;
        } catch (const lexer::LexerError &ex) {
            std::cerr << "ERROR: " << ex.what() << std::endl;
            return std::make_shared<match_type>(false, token_pos);
        }
        assert(ret->matched || ret->end == token_pos);
        ret->begin = token_pos;
        ret->name = name;
        ret->action = action;
        if (check) {
            ret->matched = check(*ret);
            if (!ret->matched)
                ret->end = token_pos;
        }
        return ret;
    }

    Rule &operator [](std::function<action_type> callable)
    {
        action = callable;
        return *this;
    }

    Rule &operator ()(std::function<check_type> callable)
    {
        check = callable;
        return *this;
    }

    std::shared_ptr<Rule> get_shared_ptr()
    {
        return this->shared_from_this();
    }
};

template <typename Iterator, typename ActionType>
class RuleResult
{
    std::shared_ptr<Rule<Iterator, ActionType>> ptr;
public:
    RuleResult() { }
    RuleResult(std::shared_ptr<Rule<Iterator, ActionType>> r_) : ptr(r_) { }
    Rule<Iterator, ActionType> *operator ->()
    {
        return ptr.get();
    }

    const Rule<Iterator, ActionType> *operator ->() const
    {
        return ptr.get();
    }
};

// This walks children who have not matched, and therefore end hasn't
// propagated, we it's necessary to go get it.
template <typename Iterator, typename ActionType>
Match<Iterator, ActionType> right_most(Match<Iterator, ActionType> &m)
{
    if (m.children.empty())
        return m;
    return right_most(*m.children.back().get());
}

template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> regex(const std::string &regex_string)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("regex"));
    boost::xpressive::sregex regex = boost::xpressive::sregex::compile(regex_string);
    rule->match = [regex](Iterator token_pos, Iterator) -> typename Rule<Iterator, ActionType>::rule_result { 
        boost::xpressive::smatch what;
        std::string token_string = *token_pos;
        bool matched = boost::xpressive::regex_match(token_string, what, regex);
        return std::make_shared<match_type>(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> terminal(int id)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("terminal"));
    rule->match = [id](Iterator token_pos, Iterator) -> typename Rule<Iterator, ActionType>::rule_result {
        bool matched = (token_pos.token == id);
        return std::make_shared<match_type>(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

// non-terminals propagate info from their children
template <typename Iterator, typename ActionType>
void propagate_child_info(Match<Iterator, ActionType> &ret, std::shared_ptr<Match<Iterator, ActionType>> child)
{
    ret.matched = child->matched;
    ret.end = child->end;
    ret.children.push_back(child->get_shared_ptr());
}

// Ordering this >> that
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator >>(RuleResult<Iterator, ActionType> first, 
                                             RuleResult<Iterator, ActionType> second)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("order"));
    rule->must_consume_token = first->must_consume_token || second->must_consume_token;
    rule->match = [first, second](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        typename Rule<Iterator, ActionType>::rule_result tmp = first->get_match(token_pos, eos);
        propagate_child_info(ret, tmp);
        if (tmp->matched) {
            tmp = second->get_match(tmp->end, eos);
            propagate_child_info(ret, tmp);
            if (!tmp->matched) {
                ret.end = token_pos;
            }
        }
        return std::make_shared<match_type>(ret);
    };
    return rule;
}

// Select this | that
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator |(RuleResult<Iterator, ActionType> first, 
                                            RuleResult<Iterator, ActionType> second)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("or"));
    rule->must_consume_token = first->must_consume_token || second->must_consume_token;
    rule->match = [first, second](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    { 
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        typename Rule<Iterator, ActionType>::rule_result tmpl, tmpr;
        tmpl = first->get_match(token_pos, eos);
        // TODO: if both fail should we propagate all the child info? 
        // Just the failure with the most children?
        if (tmpl->matched) {
            propagate_child_info(ret, tmpl);
            return std::make_shared<match_type>(ret);
        }
        tmpr = second->get_match(token_pos, eos);
        if (tmpr->matched) {
            propagate_child_info(ret, tmpr);
            return std::make_shared<match_type>(ret);
        }

        // Didn't match, see which match got further
        typename Iterator::difference_type ld = right_most(*tmpl).end - token_pos;
        typename Iterator::difference_type lr = right_most(*tmpr).end - token_pos;
        if (ld < lr) {
            propagate_child_info(ret, tmpr);
        } else {
            propagate_child_info(ret, tmpl);
        }
        return std::make_shared<match_type>(ret);
    };
    return rule;
}

// Kleene Star
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator *(RuleResult<Iterator, ActionType> first)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>(std::string("kleene->")+first->name));
    rule->must_consume_token = false;
    rule->match = [first](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        typename Rule<Iterator, ActionType>::rule_result tmp;
        ret.end = token_pos;
        Iterator tmp_pos = token_pos;
        bool tmp_matched = true;
        while (tmp_pos != eos && tmp_matched) {
            tmp = first->get_match(tmp_pos, eos);
            propagate_child_info(ret, tmp);
            tmp_pos = tmp->end;
            tmp_matched = tmp->matched;
        }
        ret.matched = true;
        return std::make_shared<match_type>(ret);
    };
    return rule;
}

// Non-greedy kleene star
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator /(RuleResult<Iterator, ActionType> first, 
                                            RuleResult<Iterator, ActionType> second)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("non-greedy kleene"));
    rule->must_consume_token = first->must_consume_token || second->must_consume_token;
    rule->match = [first, second](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        typename Rule<Iterator, ActionType>::rule_result tmp;
        ret.matched = true;
        bool matched_right_side = false;
        Iterator tmp_pos = token_pos;
        while (tmp_pos != eos) {
            Iterator start_pos = tmp_pos;
            tmp = second->get_match(start_pos, eos);
            if (tmp->matched) {
                propagate_child_info(ret, tmp);
                matched_right_side = true;
                break;
            }
            tmp = first->get_match(start_pos, eos);
            tmp_pos = tmp->end;
            propagate_child_info(ret, tmp);
            // Optional or star can return true, but didn't consume anything.
            // That means we'll loop forever.
            if (!tmp->matched || (tmp->end == start_pos)) {
                break;
            }
        }
        if (!matched_right_side) {
            ret.matched = false;
            ret.end = token_pos;
        }
        return std::make_shared<match_type>(ret);
    };
    return rule;
}

// Optional this?
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator -(RuleResult<Iterator, ActionType> first)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("optional", &first));
    rule->must_consume_token = false;
    rule->match = [first](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        if (token_pos == eos)
            return typename Rule<Iterator, ActionType>::match_type(true, eos);
        typename Rule<Iterator, ActionType>::match_type ret, tmp = first->get_match(token_pos, eos);
        propagate_child_info(ret, tmp);
        assert(ret.matched || (tmp->end == token_pos));
        ret.matched = true;
        return std::make_shared<match_type>(ret);
    };
    return rule;
}

// 1 or more
template <typename Iterator, typename ActionType>
RuleResult<Iterator, ActionType> operator +(RuleResult<Iterator, ActionType> first)
{
    return (first >> (*first));
}

}

#endif

