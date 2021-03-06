#pragma once
// SDP parser
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace strophe
{
    class Stanza;
}

namespace sdpUtil
{
typedef std::map<std::string, std::string> StringMap;
typedef std::vector<std::string> LineGroup;

struct MLine
{
    std::string media;
    std::string port;
    std::string proto;
    std::vector<std::string> fmt;
    MLine(const std::string& line);
    MLine(strophe::Stanza content);
    std::string toSdp();
};
struct MGroup: public LineGroup
{
    MLine mline;
    MGroup(const strophe::Stanza stanza): mline(stanza){}
    MGroup(const std::string& strMline): mline(strMline){}
    const std::string& name() const { return mline.media; }
};

class ParsedSdp
{
public:
    /** Each media element is a group of lines related to one 'm=' block in the SDP */
    std::vector<MGroup> media;
    /** This is the group of lines that is before the first 'm=' group of lines */
    LineGroup session;
/// construct from SDP
    void parse(const std::string& strSdp);
/// construct from a jingle stanza
    void parse(strophe::Stanza jingle);
    std::string toString() const;
/// checks if there is an 'm=<name>:' line
    int getMlineIndex(const std::string& mid);
/// add contents to a jingle element
    strophe::Stanza toJingle(strophe::Stanza elem, const char* creator);
protected:
    void rtcpFbFromJingle(strophe::Stanza elem, const std::string& payloadtype, LineGroup& media);
/// translate a jingle content element into an an SDP media part
    std::unique_ptr<MGroup> jingle2media(strophe::Stanza content);
};

std::unique_ptr<StringMap> parse_rtpmap(const std::string& line, const std::string& id);
std::string build_rtpmap(strophe::Stanza el);
std::unique_ptr<StringMap> parse_crypto(const std::string& line);
std::unique_ptr<std::vector<std::pair<std::string, std::string> > >
    parse_fmtp(const std::string& line);
std::unique_ptr<StringMap> parse_extmap(const std::string& line);
template <int flags = 0> std::string
find_line(const LineGroup& haystack, const std::string& needle, size_t& start);
template <int flags = 0> std::string
find_line(const LineGroup& haystack, const std::string& needle);
template <int flags = 0> std::string
find_line(const LineGroup& haystack, const std::string& needle, const LineGroup& session);
template <int flags = 0> std::unique_ptr<LineGroup>
find_lines(const LineGroup& haystack, const std::string& needle);
template <int flags = 0> std::unique_ptr<LineGroup>
find_lines(const LineGroup& haystack, const std::string& needle, const LineGroup& sessionpart);
std::unique_ptr<StringMap> iceparams(const LineGroup& mediadesc, const LineGroup& sessiondesc);
std::unique_ptr<StringMap> candidateToJingle(const std::string& line);
std::string candidateFromJingle(strophe::Stanza cand, bool isInSdp = false);
std::string parse_fingerprint(const std::string& line, StringMap& attrs);
}
