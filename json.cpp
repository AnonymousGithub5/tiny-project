#include <iostream>
#include <variant>
#include <vector>
#include <map>
#include <optional>
#include <string>
#include <fstream>
#include <sstream>

namespace json {
    
    struct Node;
    using Null = std::monostate;
    using Bool = bool;
    using Int = int64_t;
    using Float = double;
    using String = std::string;
    using Array = std::vector<Node>;
    using Object = std::map<std::string, Node>;
    using Value = std::variant<Null, Bool, Int, Float, String, Array, Object>;
    struct Node {
        Value value;  // 可能是各种类型的值
        Node(Value _value) : value(_value) {}
        Node() : value(Null{}) {}
        auto& operator[](const std::string& key) {  // Node1["abc"]
            if (auto object = std::get_if<Object>(&value)) {
                return  (*object)[key];
            }
            throw std::runtime_error("not an object");
        }
        auto operator[](size_t index) {  // Node2[2]
            if (auto array = std::get_if<Array>(&value)) {
                return array->at(index);
            }
            throw std::runtime_error("not an array");
        }
        void push(const Node& rhs) {  // Node3.push(Node4)
            if (auto array = std::get_if<Array>(&value)) {
                array->push_back(rhs);
                return;
            }
            throw std::runtime_error("not an array push");
        }
    };

    struct JsonParser {
        std::string_view json_str;
        size_t pos = 0;

        void parse_whitespace() {
            while (pos < json_str.size() && std::isspace(json_str[pos])) {
                ++pos;
            }
        }

        auto parse_null() -> std::optional<Value> {
            if (json_str.substr(pos, 4) == "null") {
                pos += 4;
                return Null{};
            }
            return{};
        }

        auto parse_true() -> std::optional<Value> {
            if (json_str.substr(pos, 4) == "true") {
                pos += 4;
                return true;
            }
            return {};
        }

        auto parse_false() -> std::optional<Value> {
            if (json_str.substr(pos, 5) == "false") {
                pos += 5;
                return false;
            }
            return {};
        }

        auto parse_number()->std::optional<Value> {
            size_t endpos = pos;
            while (endpos < json_str.size() && (
                std::isdigit(json_str[endpos]) ||
                json_str[endpos] == 'e' ||
                json_str[endpos] == '.')) {
                endpos++;
            }
            std::string number = std::string{ json_str.substr(pos, endpos - pos) };
            pos = endpos;
            static auto is_Float = [](std::string& number) {
                return number.find('.') != number.npos || number.find('e') != number.npos;
            };
            if (is_Float(number)) {
                try {
                    Float ret = std::stod(number);
                    return ret;
                }
                catch (...) {
                    return {};
                }
            }
            else {
                try {
                    Int ret = std::stoi(number);
                    return ret;
                }
                catch (...) {
                    return {};
                }
            }
        }

        auto parse_string() -> std::optional<Value> {
            pos++;  // "
            size_t endpos = pos;
            while (pos < json_str.size() && json_str[endpos] != '"') {
                endpos++;
            }
            std::string str = std::string{ json_str.substr(pos, endpos - pos) };
            pos = endpos + 1;  // "
            return str;
        }

        auto parse_array() -> std::optional<Value> {
            pos++;// [
            Array arr;
            while (pos < json_str.size() && json_str[pos] != ']') {
                auto value = parse_value();
                arr.push_back(value.value());
                parse_whitespace();
                if (pos < json_str.size() && json_str[pos] == ',') {
                    pos++;// ,
                }
                parse_whitespace();
            }
            pos++;// ]
            return arr;
        }

        std::optional<Value> parse_object() {
            pos++;  // {
            Object obj;
            while (pos < json_str.size() && json_str[pos] != '}') {
                auto key = parse_value();
                parse_whitespace();
                if (!std::holds_alternative<String>(key.value())) {  // key如果不是string类型的,那就结束(出错)
                    return {};
                }
                if (pos < json_str.size() && json_str[pos] == ':') {
                    pos++;  // :
                }
                parse_whitespace();
                auto val = parse_value();  // 递归
                obj[std::get<String>(key.value())] = val.value();
                parse_whitespace();
                if (pos < json_str.size() && json_str[pos] == ',') {
                    pos++;// ,
                }
                parse_whitespace();
            }
            pos++;// }
            return obj;

        }

        std::optional<Value> parse_value() {
            parse_whitespace();
            switch (json_str[pos]) {
                case 'n':
                    return parse_null();
                case 't':
                    return parse_true();  // 返回一个bool的true,或者{}
                case 'f':
                    return parse_false();
                case '"':
                    return parse_string();
                case '[':
                    return parse_array();
                case '{':
                    return parse_object();
                default:
                    return parse_number();
            }
        }

        std::optional<Node> parse() {
            parse_whitespace();
            auto value = parse_value();
            if (!value) {
                return {};  // 如果parse_value()返回一个nullopt,说明解析失败,
            }
            return Node{*value};
        }
    };


    // {"config": "yaml", "lr": [0.5, 0.6], "dropout": true}
    // 输入json文件中的字符串,用这个字符串来构造一个JsonParser对象
    std::optional<Node> parser(std::string_view json_str) {
        JsonParser p{json_str};
        return p.parse();
    }


    class JsonGenerator {
    public:
        static auto generate(const Node& node) -> std::string {
            return std::visit(
                [](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Null>) {
                        return "null";
                    }
                    else if constexpr (std::is_same_v<T, Bool>) {
                        return arg ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, Int>) {
                        return std::to_string(arg);
                    }
                    else if constexpr (std::is_same_v<T, Float>) {
                        return std::to_string(arg);
                    }
                    else if constexpr (std::is_same_v<T, String>) {
                        return generate_string(arg);
                    }
                    else if constexpr (std::is_same_v<T, Array>) {
                        return generate_array(arg);
                    }
                    else if constexpr (std::is_same_v<T, Object>) {
                        return generate_object(arg);
                    }
                },
                node.value
            );
        }
        static auto generate_string(const String& str) -> std::string {
            std::string json_str = "\"";
            json_str += str;
            json_str += '"';
            return json_str;
        }
        static auto generate_array(const Array& array) -> std::string {
            std::string json_str = "[";
            for (const auto& node : array) {
                json_str += generate(node);
                json_str += ',';
            }
            if (!array.empty()) {
                json_str.pop_back();
            }
            json_str += ']';
            return json_str;
        }
        static auto generate_object(const Object& object) -> std::string {
            std::string json_str = "{";
            for (const auto& [key, node] : object) {
                json_str += generate_string(key);
                json_str += ':';
                json_str += generate(node);
                json_str += ',';
            }
            if (!object.empty()) {
                json_str.pop_back();
            }
            json_str += '}';
            return json_str;
        }
    };

    
    inline std::string generate(const Node& node) {
        return JsonGenerator::generate(node);
    }


    std::ostream& operator << (std::ostream& out, const Node& t) {
        out << JsonGenerator::generate(t);
        return out;
    }
    
}

using namespace json;

int main() {
    std::ifstream fin("json.txt");
    std::stringstream ss;
    ss << fin.rdbuf();  // stringstream <- ifstream
    std::string s{ss.str()};

    auto x = parser(s).value();
    std::cout << x << "\n";
    x["configurations"].push({true});
    x["configurations"].push({Null {}});
    // x["version"] = { 114514LL };
    std::cout << x << "\n\n";
}