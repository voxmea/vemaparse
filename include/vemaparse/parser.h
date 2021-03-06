
#ifndef VEMAPARSE_PARSER_H_
#define VEMAPARSE_PARSER_H_

#include <tuple>
#include <functional>
#include <vector>
#include <list>
#include <map>
#include <deque>
#include <iterator>
#include <algorithm>
#include <memory>

#include <regex>

namespace vemaparse
{

template <typename Iterator, typename ActionType>
struct Match : std::enable_shared_from_this<Match<Iterator, ActionType>>
{
    typedef std::shared_ptr<Match> match_shared_ptr;
    bool matched;
    std::string name;
    Iterator begin, end;
    std::function<void(ActionType &)> action;
    std::deque<match_shared_ptr> children;

    Match(Iterator end_) : matched(false), end(end_) { }
    Match(bool matched_, Iterator end_) : matched(matched_), end(end_) { }

    match_shared_ptr get_shared_ptr() {return this->shared_from_this();}
};

template <typename Iterator, typename ActionType>
std::string to_string(const Match<Iterator, ActionType> &m)
{
    std::string ret;
    std::for_each(m.begin, m.end, [&ret](const std::string &s) {ret += s;});
    return ret;
}

template <typename Iterator, typename ActionType> class RuleWrapper;

template <typename Iterator, typename ActionType>
struct Rule : std::enable_shared_from_this<Rule<Iterator, ActionType>>
{
    typedef Match<Iterator, ActionType> match_type;
    typedef std::shared_ptr<match_type> rule_result;
    typedef void action_type(ActionType &);
    typedef bool check_type(const match_type &);
    typedef Iterator iterator;
    mutable std::map<Iterator, rule_result> cache;

    std::string name;
    std::function<action_type> action;
    std::function<check_type> check;
    std::function<rule_result(Iterator, Iterator)> match;
    bool must_consume_token;
    std::vector<RuleWrapper<Iterator, ActionType>> children;

    Rule() : must_consume_token(true) { }
    Rule(const std::string name_) : name(name_), must_consume_token(true) { }

    // Use this to break shared_ptr cycles
    void reset();

    rule_result get_match(Iterator token_pos, Iterator eos) const
    {
        if (must_consume_token && token_pos == eos)
            return std::make_shared<match_type>(eos);
        if (cache.find(token_pos) != cache.end())
            return cache[token_pos];
        rule_result ret;
        try {
            // static int depth = 0;
            // std::fill_n(std::ostream_iterator<char>(std::cout), depth, ' ');
            // std::cout << depth++ << ":trying " << name << " on \"" << *token_pos << "\"" << std::endl;
            ret = match(token_pos, eos);
            // depth--;
        } catch (const vemalex::LexerError &ex) {
            std::cerr << "ERROR: " << ex.what() << std::endl;
            // assert(0);
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
        cache[token_pos] = ret;
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
class RuleWrapper
{
    std::shared_ptr<Rule<Iterator, ActionType>> ptr;

public:
    typedef Iterator iterator;
    typedef typename Rule<Iterator, ActionType>::rule_result rule_result;
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    RuleWrapper() { }
    RuleWrapper(std::shared_ptr<Rule<Iterator, ActionType>> r_) : ptr(r_) { }

    RuleWrapper &operator =(const RuleWrapper &other)
    {
        if (this == &other)
            return *this;

        if (ptr) {
            ptr->match = other->match;
            ptr->must_consume_token = other->must_consume_token;
            ptr->children = other->children;
            if (other->check)
                ptr->check = other->check;
            if (other->action)
                ptr->action = other->action;
            if (other->name.size()) {
                ptr->name = other->name;
            }
            return *this;
        }

        ptr = other.ptr;
        return *this;
    }

    Rule<Iterator, ActionType> *operator ->()
    {
        assert(ptr);
        return ptr.get();
    }

    const Rule<Iterator, ActionType> *operator ->() const
    {
        assert(ptr);
        return ptr.get();
    }

    template <typename T>
    RuleWrapper &operator [](T action)
    {
        assert(ptr);
        ptr->action = action;
        return *this;
    }

    template <typename T>
    RuleWrapper &operator ()(T check)
    {
        assert(ptr);
        ptr->check = check;
        return *this;
    }

    void reset()
    {
        assert(ptr);
        ptr->reset();
        ptr.reset();
    }

    static RuleWrapper create_empty_rule()
    {
        return std::make_shared<Rule<Iterator, ActionType>>();
    }

    static RuleWrapper clone_rule(const RuleWrapper &other)
    {
        return RuleWrapper(std::make_shared<Rule<Iterator, ActionType>>(*other.ptr));
    }
};

template <typename Iterator, typename ActionType>
inline void Rule<Iterator, ActionType>::reset()
{
    name = "";
    action = std::function<action_type>();
    check = std::function<check_type>();
    match = std::function<rule_result(Iterator, Iterator)>();
    // Don't want to recurse, so make a copy and then clear children before iterating.
    auto children_copy = children;
    children.clear();
    for (auto iter = children_copy.begin(); iter != children_copy.end(); ++iter) {
        (*iter)->reset();
    }
}

// This walks children who have not matched, and therefore end hasn't
// propagated, therefore it's necessary to go get it.
template <typename Iterator, typename ActionType>
Match<Iterator, ActionType> right_most(Match<Iterator, ActionType> &m)
{
    if (m.children.empty())
        return m;
    return right_most(*m.children.back().get());
}

template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> regex(const std::string &regex_string)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("regex"));
    std::regex re = std::regex(regex_string);
    rule->match = [re](Iterator token_pos, Iterator) -> typename Rule<Iterator, ActionType>::rule_result { 
        std::string token_string = *token_pos;
        bool matched = std::regex_match(token_string, re);
        return std::make_shared<match_type>(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> terminal(int id)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("terminal"));
    rule->match = [id](Iterator token_pos, Iterator) -> typename Rule<Iterator, ActionType>::rule_result {
        bool matched = (token_pos.token == id);
        return std::make_shared<match_type>(matched, matched ? ++token_pos : token_pos);
    };
    return rule;
}

template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> newline(RuleWrapper<Iterator, ActionType> first)
{
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("newline"));
    rule->match = [first](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result {
        token_pos.start_newline();
        auto ret = first->get_match(token_pos, eos);
        ret->end.stop_newline();
        return ret;
    };
    rule->children.push_back(first);
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
RuleWrapper<Iterator, ActionType> operator >>(RuleWrapper<Iterator, ActionType> first, 
                                              RuleWrapper<Iterator, ActionType> second)
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
    rule->children.push_back(first);
    rule->children.push_back(second);
    return rule;
}

// Select this | that
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator |(RuleWrapper<Iterator, ActionType> first, 
                                             RuleWrapper<Iterator, ActionType> second)
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
    rule->children.push_back(first);
    rule->children.push_back(second);
    return rule;
}

// Kleene Star
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator *(RuleWrapper<Iterator, ActionType> first)
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
    rule->children.push_back(first);
    return rule;
}

// Non-greedy kleene star
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator /(RuleWrapper<Iterator, ActionType> first, 
                                             RuleWrapper<Iterator, ActionType> second)
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
    rule->children.push_back(first);
    rule->children.push_back(second);
    return rule;
}

// Optional this?
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator -(RuleWrapper<Iterator, ActionType> first)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("optional"));
    rule->must_consume_token = false;
    rule->match = [first](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        if (token_pos == eos)
            return std::make_shared<match_type>(ret);
        typename Rule<Iterator, ActionType>::rule_result tmp = first->get_match(token_pos, eos);
        propagate_child_info(ret, tmp);
        assert(ret.matched || (tmp->end == token_pos));
        ret.matched = true;
        return std::make_shared<match_type>(ret);
    };
    rule->children.push_back(first);
    return rule;
}

// 1 or more
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator +(RuleWrapper<Iterator, ActionType> first)
{
    return (first >> (*first));
}

// Not
template <typename Iterator, typename ActionType>
RuleWrapper<Iterator, ActionType> operator !(RuleWrapper<Iterator, ActionType> first)
{
    typedef typename Rule<Iterator, ActionType>::match_type match_type;
    std::shared_ptr<Rule<Iterator, ActionType>> rule(new Rule<Iterator, ActionType>("not"));
    rule->match = [first](Iterator token_pos, Iterator eos) -> typename Rule<Iterator, ActionType>::rule_result 
    {
        typename Rule<Iterator, ActionType>::match_type ret(eos);
        if (token_pos == eos)
            return std::make_shared<match_type>(ret);
        typename Rule<Iterator, ActionType>::rule_result tmp = first->get_match(token_pos, eos);
        const bool matched = !tmp->matched;
        return std::make_shared<match_type>(matched, matched ? ++token_pos : token_pos);
    };
    rule->children.push_back(first);
    return rule;
}

}

#endif

