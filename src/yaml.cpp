#include "yaml.hpp"
#include "yaml_macros.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <sstream>


namespace dr {

using namespace std::string_literals;

YamlError::YamlError(std::string message, std::vector<YamlNodeDescription> trace) :
	message{std::move(message)},
	trace{std::move(trace)} {}

YamlError & YamlError::appendTrace(YamlNodeDescription description) & {
	trace.push_back(std::move(description));
	return *this;
}

YamlError && YamlError::appendTrace(YamlNodeDescription description) && {
	trace.push_back(std::move(description));
	return std::move(*this);
}

std::string YamlError::formatTrace() const {
	if (trace.empty()) return "";

	std::string result = trace.back().name;

	for (auto i = trace.rbegin(); i != trace.rend(); ++i) {
		// Skip the first node (already dealt with it).
		if (i == trace.rbegin()) continue;
		auto prev = i - 1;
		if (prev->node_type == YAML::NodeType::Sequence) {
			result.push_back('[');
			result.append(i->name);
			result.push_back(']');
		} else {
			result.push_back('.');
			result.append(i->name);
		}
	}

	return result;
}

std::string YamlError::format() const {
	if (trace.empty()) return message;
	return formatTrace() + ": " + message;
}

std::string toString(YAML::NodeType::value type) {
	switch (type) {
		case YAML::NodeType::Map:       return "map";
		case YAML::NodeType::Null:      return "null";
		case YAML::NodeType::Scalar:    return "scalar";
		case YAML::NodeType::Sequence:  return "sequence";
		case YAML::NodeType::Undefined: return "undefined";
	}
	return "unknown";
}

std::optional<YamlError> expectMap(YAML::Node const & node) {
	if (!node) return YamlError{"no such node"};
	if (!node) return std::nullopt;
	if (node.IsMap()) return std::nullopt;
	return YamlError{fmt::format("invalid node type: expected map, got {}", toString(node.Type()))};
}

std::optional<YamlError> expectMap(YAML::Node const & node, std::size_t size) {
	if (auto error = expectMap(node)) return error;
	if (node.size() == size) return std::nullopt;
	return YamlError{fmt::format("invalid map size: expected {} child nodes, got {}",size, node.size())};
}

std::optional<YamlError> expectSequence(YAML::Node const & node) {
	if (!node) return YamlError{"no such node"};
	if (node.IsSequence()) return std::nullopt;
	return YamlError{fmt::format("invalid node type: expected list, got {}", toString(node.Type()))};
}
std::optional<YamlError> expectSequence(YAML::Node const & node, std::size_t size) {
	if (auto error = expectSequence(node)) return error;
	if (node.size() == size) return std::nullopt;
	return YamlError{fmt::format("invalid list size: expected {} elements, got {}", size, node.size())};
}

std::optional<YamlError> expectScalar(YAML::Node const & node) {
	if (!node) return YamlError{"no such node"};
	if (node.IsScalar()) return std::nullopt;
	return YamlError{fmt::format("invalid node type: expected scalar, got {}", toString(node.Type()))};
}

estd::result<YAML::Node, estd::error> readYamlFile(std::string const & path) {
	std::ifstream file(path);
	if (!file.good()) {
		int error = errno;
		return estd::error{{error, std::system_category()}, path};
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return YAML::Load(buffer.str());
}
// Merge Yaml Nodes of type Ordered Dictionary, i.e., sequence on Maps.
YamlResult<void> mergeYamlOrderedDict(YAML::Node & map_a, YAML::Node map_b);

inline YamlResult<void> mergeYamlOrderedDict(YAML::Node && map_a, YAML::Node map_b) {
 	return mergeYamlOrderedDict(map_a, map_b);
 }

// Merge Yaml Nodes of type Map , i.e., a key-value pair.
YamlResult<void> mergeYamlMaps(YAML::Node & map_a, YAML::Node map_b);

inline YamlResult<void> mergeYamlMaps(YAML::Node && map_a, YAML::Node map_b) {
 	return mergeYamlMaps(map_a, map_b);
  }


// Merge Yaml Nodes (Type: Map).
YamlResult<void> mergeYamlMaps(YAML::Node & map_a, YAML::Node map_b) {
	if (map_b.IsMap()){
		for (YAML::const_iterator iterator = map_b.begin(); iterator != map_b.end(); iterator++) {
			std::string key = iterator->first.as<std::string>();
			YAML::Node value = iterator->second;
			if (map_a[key] && map_a[key].IsMap()) {
				auto merged = mergeYamlMaps(map_a[key], value);
				if (!merged) {
					merged.error().appendTrace({key, "", YAML::NodeType::Map});
					return merged;
				}
			}
			else if (value.Tag() == "!ordered_dict"  && map_a[key].Tag() == "!ordered_dict" && map_a[key]) {
				auto merged = mergeYamlOrderedDict(map_a[key], value);
				if (!merged) {
					merged.error().appendTrace({key, "", YAML::NodeType::Map});
					return merged;
				}
			}
			else {
			map_a[key] = value;
			}
		}
	}
	return estd::in_place_valid;
}

// Uitility function to check whether a value is present in a vector.
bool Contains(const std::vector<int> &list, int x){
	return std::find(list.begin(), list.end(), x) != list.end();
}

// Check whether item is a single item map.
bool singleMap(YAML::Node yaml_node){
	if (!yaml_node.IsMap() && yaml_node.size() > 1){
		return false;
	}
	return true;
}
// Utility function to check whether key in map_b is present in map_a.
std::optional<std::size_t> orderedDictFind(YAML::Node map_a, std::string const & key){
	for (std::size_t j = 0; j < map_a.size(); j++){
			if (map_a[j][key]){
					return j;
				}
			
	}
	
	return std::nullopt;
}

// Merge ordered dictionaries.
YamlResult<void> mergeYamlOrderedDict(YAML::Node & map_a, YAML::Node map_b){
	// Check if map_b is a list which is the first criteria for an ordered dictionary.
	if (!map_b.IsSequence()){
		return YamlError("Child node is not an ordered dictionary.");
	}

	// Vector containing all the indexes of b which are present in a.
	std::vector<int> checked;
	std::optional<std::size_t> index;
	bool single_map = true;
	// Boolean to determine whether the element in b is present in a or not.
	bool found = false;
	for (std::size_t i = 0; i < map_b.size(); i++) {
		found = false;
		// Check whether the element in b is a single key-value pair.
		if (!singleMap(map_b[i])){
			return YamlError("Ordered dictionary should only contain single item map");
		}
		std::string key = map_b[i].begin()->first.as<std::string>();
		index = orderedDictFind(map_a, key);
		if (index && !Contains(checked, index.value())) {
			if (!singleMap(map_a[index.value()])) {
				return YamlError("Ordered dictionary should only contain single item map");
			}
			mergeYamlMaps(map_a[index.value()], map_b[i]);
			checked.push_back(index.value());
			found = true;
		}
		
		// If element in b is not present in a, push the new element to a.
		if (!found){
				map_a.push_back(map_b[i]);
		}
	}
	
	return estd::in_place_valid;
}

// Merge Yaml Nodes.
YamlResult<void> mergeYamlNodes(YAML::Node & map_a, YAML::Node map_b) {
	if (map_a.IsMap()){
		mergeYamlMaps(map_a, map_b);
	}
	else if (map_a.IsNull() && !map_b.IsNull()){
		map_a = map_b;
	}
	else if (map_b.IsNull() && !map_a.IsNull()) {
		return estd::in_place_valid;
	} 
	else if (map_a.Tag() == "!ordered_dict") {
		mergeYamlOrderedDict(map_a, map_b);
	}
	
	return estd::in_place_valid;
}


}

// New style YAML conversions.
using namespace dr;

namespace {

	template<typename T>
	YamlResult<T> convert_signed_integral(YAML::Node const & node) {
		if (auto error = expectScalar(node)) return *error;

		std::string const & raw = node.Scalar();
		std::size_t parsed = 0;
		long long value = 0;
		try {
			value = std::stoll(raw, &parsed);
		} catch (std::invalid_argument &e) {
			return YamlError{"invalid integer value: " + raw};
		} catch (std::range_error const & e) {
			return YamlError{"integer value out of range: " + raw};
		}

		if (parsed != raw.size()) return YamlError{"invalid integer value: " + raw};
		if (value > std::numeric_limits<T>::max())    YamlError{"integer value out of range: " + raw};
		if (value < std::numeric_limits<T>::lowest()) YamlError{"integer value out of range: " + raw};
		return T(value);
	}

	template<typename T>
	YamlResult<T> convert_unsigned_integral(YAML::Node const & node) {
		if (auto error = expectScalar(node)) return *error;

		std::string const & raw = node.Scalar();
		std::size_t parsed = 0;
		unsigned long long value = 0;
		try {
			value = std::stoull(raw, &parsed);
		} catch (std::invalid_argument &e) {
			return YamlError{"invalid integer value: " + raw};
		} catch (std::range_error const & e) {
			return YamlError{"integer value out of range: " + raw};
		}

		if (parsed != raw.size()) return YamlError{"invalid integer value: " + raw};
		if (value > std::numeric_limits<T>::max())    YamlError{"integer value out of range: " + raw};
		if (value < std::numeric_limits<T>::lowest()) YamlError{"integer value out of range: " + raw};
		return T(value);
	}

	template<typename T>
	YamlResult<T> convert_floating_point(YAML::Node const & node) {
		if (auto error = expectScalar(node)) return *error;

		std::string const & raw = node.Scalar();
		std::size_t parsed = 0;
		long double value = 0;
		try {
			value = std::stold(raw, &parsed);
		} catch (std::invalid_argument &e) {
			return YamlError{"invalid floating point value: " + raw};
		} catch (std::range_error const & e) {
			return YamlError{"floating point value out of range: " + raw};
		}

		if (parsed != raw.size()) return YamlError{"invalid floating point value: " + raw};
		return T(value);
	}

}

DR_PARAM_DEFINE_YAML_DECODE(std::string, node) {
	if (auto error = expectScalar(node)) return *error;
	return node.Scalar();
}

DR_PARAM_DEFINE_YAML_ENCODE(std::string, value) {
	return YAML::Node(std::move(value));
}

DR_PARAM_DEFINE_YAML_DECODE(std::string_view, node) {
	if (auto error = expectScalar(node)) return *error;
	return std::string_view{node.Scalar()};
}

DR_PARAM_DEFINE_YAML_ENCODE(std::string_view, value) {
	return YAML::Node(std::string{value});
}

DR_PARAM_DEFINE_YAML_DECODE(bool, node) {
	if (auto error = expectScalar(node)) return *error;

	std::string raw = node.Scalar();
	std::transform(raw.begin(), raw.end(), raw.begin(), [] (char c) { return std::tolower(c); });

	if (raw == "y" || raw == "yes" || raw == "true"  || raw == "on"  || raw == "1") return true;
	if (raw == "n" || raw == "no"  || raw == "false" || raw == "off" || raw == "0") return false;
	return dr::YamlError{"invalid boolean value: " + node.Scalar()};
}

YAML::Node estd::conversion<bool, YAML::Node>::perform(bool value) { return YAML::Node(value); }

DR_PARAM_DEFINE_YAML_DECODE(char     , node) { return convert_signed_integral<char     >(node); }
DR_PARAM_DEFINE_YAML_DECODE(short    , node) { return convert_signed_integral<short    >(node); }
DR_PARAM_DEFINE_YAML_DECODE(int      , node) { return convert_signed_integral<int      >(node); }
DR_PARAM_DEFINE_YAML_DECODE(long     , node) { return convert_signed_integral<long     >(node); }
DR_PARAM_DEFINE_YAML_DECODE(long long, node) { return convert_signed_integral<long long>(node); }

DR_PARAM_DEFINE_YAML_DECODE(unsigned char     , node) { return convert_unsigned_integral<unsigned char     >(node); }
DR_PARAM_DEFINE_YAML_DECODE(unsigned short    , node) { return convert_unsigned_integral<unsigned short    >(node); }
DR_PARAM_DEFINE_YAML_DECODE(unsigned int      , node) { return convert_unsigned_integral<unsigned int      >(node); }
DR_PARAM_DEFINE_YAML_DECODE(unsigned long     , node) { return convert_unsigned_integral<unsigned long     >(node); }
DR_PARAM_DEFINE_YAML_DECODE(unsigned long long, node) { return convert_unsigned_integral<unsigned long long>(node); }

DR_PARAM_DEFINE_YAML_DECODE(float      , node) { return convert_floating_point<float>      (node); }
DR_PARAM_DEFINE_YAML_DECODE(double     , node) { return convert_floating_point<double>     (node); }
DR_PARAM_DEFINE_YAML_DECODE(long double, node) { return convert_floating_point<long double>(node); }

DR_PARAM_DEFINE_YAML_DECODE(YAML::Node, node) { return node; }

YAML::Node estd::conversion<char,      YAML::Node>::perform(char      value) { return YAML::Node(value); }
YAML::Node estd::conversion<short,     YAML::Node>::perform(short     value) { return YAML::Node(value); }
YAML::Node estd::conversion<int,       YAML::Node>::perform(int       value) { return YAML::Node(value); }
YAML::Node estd::conversion<long,      YAML::Node>::perform(long      value) { return YAML::Node(value); }
YAML::Node estd::conversion<long long, YAML::Node>::perform(long long value) { return YAML::Node(value); }

YAML::Node estd::conversion<unsigned char,      YAML::Node>::perform(unsigned char      value) { return YAML::Node(value); }
YAML::Node estd::conversion<unsigned short,     YAML::Node>::perform(unsigned short     value) { return YAML::Node(value); }
YAML::Node estd::conversion<unsigned int,       YAML::Node>::perform(unsigned int       value) { return YAML::Node(value); }
YAML::Node estd::conversion<unsigned long,      YAML::Node>::perform(unsigned long      value) { return YAML::Node(value); }
YAML::Node estd::conversion<unsigned long long, YAML::Node>::perform(unsigned long long value) { return YAML::Node(value); }

YAML::Node estd::conversion<float,       YAML::Node>::perform(float       value) { return YAML::Node(value); }
YAML::Node estd::conversion<double,      YAML::Node>::perform(double      value) { return YAML::Node(value); }
YAML::Node estd::conversion<long double, YAML::Node>::perform(long double value) { return YAML::Node(value); }

YAML::Node estd::conversion<YAML::Node, YAML::Node>::perform(YAML::Node value) { return value; }
