#pragma once
#include <estd/result.hpp>
#include <estd/convert/convert.hpp>
#include <estd/convert/traits.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>

/**
 * This header defines a system to convert complex structs to/from YAML representation.
 *
 * Note that yamlcpp already comes with a system like this,
 * but that system has almost no error-reporting capabilities.
 *
 * This system is based on estd::convert and uses the YamlError type to expose more details,
 * and in the end provide the user with better error messages.
 */

namespace dr {

/// Description of a YAML node in a node tree.
struct YamlNodeDescription {
	std::string name;
	std::string user_type;
	YAML::NodeType::value node_type;
};

/// An error that occured during the conversion of a node tree to an object.
struct YamlError {
	/// A human readable description of the error.
	std::string message;

	/// A trace through the node tree to the root node.
	std::vector<YamlNodeDescription> trace;

	/// Create a new YAML error.
	explicit YamlError(std::string message, std::vector<YamlNodeDescription> node_path = {});

	/// Append a node description to the trace.
	YamlError  & appendTrace(YamlNodeDescription description) &;
	YamlError && appendTrace(YamlNodeDescription description) &&;

	/// Format the node trace as a string.
	std::string formatTrace() const;

	/// Format the whole error as a string.
	std::string format() const;
};

/// Result type used by YAML parsing functions.
template<typename T>
using YamlResult = estd::result<T, YamlError>;

/// Trait to check if a type can be converted from a YAML::Node.
template<typename T>
constexpr bool can_encode_yaml = estd::can_convert<T, YAML::Node>;

/// Trait to check if a type can be converted from a YAML::Node.
template<typename T>
constexpr bool can_parse_yaml = estd::can_convert<YAML::Node, YamlResult<T>>;

/// Parse a YAML::Node into a type T.
template<typename T>
YamlResult<T> parseYaml(YAML::Node const & node) {
	static_assert(can_parse_yaml<T>, "No YAML conversion defined for T");
	return estd::parse<T, YamlError>(node);
}

/// Encode a value T into a YAML::Node.
template<typename T>
YAML::Node encodeYaml(T const & value) {
	static_assert(can_encode_yaml<T>, "No YAML conversion defined for T");
	return estd::convert<YAML::Node>(value);
}

/// Test if a node is a map, and if not, return a YamlError with a descriptive error message.
std::optional<YamlError> expectMap(YAML::Node const & node);

/// Test if a node is a map with a given size, and if not, return a YamlError with a descriptive error message.
std::optional<YamlError> expectMap(YAML::Node const & node, std::size_t size);

/// Test if a node is a sequence, and if not, return a YamlError with a descriptive error message.
std::optional<YamlError> expectSequence(YAML::Node const & node);

/// Test if a node is a sequence with a given size, and if not, return a YamlError with a descriptive error message.
std::optional<YamlError> expectSequence(YAML::Node const & node, std::size_t size);

/// Test if a node is a scalar, and if not, return a YamlError with a descriptive error message.
std::optional<YamlError> expectScalar(YAML::Node const & node);

/// Convert a node type to string.
/**
 * Used amongst others to report incorrect types in error messages.
 */
std::string toString(YAML::NodeType::value);

/// Read a YAML file from a path.
/**
 * This should be preferred over YAML::LoadFile(...),
 * because this function has much better error reporting.
 */
estd::result<YAML::Node, estd::error> readYamlFile(std::string const & path);

/// Merge Yaml Node b to a.
YamlResult<void> mergeYamlNodes(YAML::Node & map_a, YAML::Node map_b);

/// Set a variable to a subkey of a node if it exists.
/**
 * This function is deprecated and should not be used.
 *
 * Use the parseYaml(...) function instead.
 */
template<typename T>
[[deprecated]]
void setIfExists(T & output, YAML::Node const & node, std::string const & key) {
	if (node[key]) output = node[key].as<T>();
}

/// Convert a child node to a given type with slightly better error message.
/**
 * This function is deprecated and should not be used.
 *
 * Use the parseYaml(...) function instead.
 */
template<typename T>
[[deprecated]]
estd::result<T, estd::error> convertChild(YAML::Node const & node, std::string const & key) {
	if (!node[key]) return estd::error{std::errc::invalid_argument, "no such key: " + key};
	try {
		return node[key].as<T>();
	} catch (std::system_error const & e) {
		return estd::error{e.code(), std::string{"failed to convert node: "} + e.what()};
	} catch (std::exception const & e) {
		return estd::error{std::errc::invalid_argument, std::string{"failed to convert node: "} + e.what()};
	}
}

}

namespace estd {

/**
 * The bits here define basic conversion between common types and YAML nodes.
 * That includes primitive types and containers from the standard library.
 */

#define DECLARE_YAML_CONVERSION(TYPE) template<> struct conversion<YAML::Node, dr::YamlResult<TYPE>> { \
	static dr::YamlResult<TYPE> perform(YAML::Node const &); \
}; \
template<> struct conversion<TYPE, YAML::Node> { \
	static YAML::Node perform(TYPE); \
}


DECLARE_YAML_CONVERSION(bool);
DECLARE_YAML_CONVERSION(char);
DECLARE_YAML_CONVERSION(short);
DECLARE_YAML_CONVERSION(int);
DECLARE_YAML_CONVERSION(long);
DECLARE_YAML_CONVERSION(long long);
DECLARE_YAML_CONVERSION(unsigned char);
DECLARE_YAML_CONVERSION(unsigned short);
DECLARE_YAML_CONVERSION(unsigned int);
DECLARE_YAML_CONVERSION(unsigned long);
DECLARE_YAML_CONVERSION(unsigned long long);
DECLARE_YAML_CONVERSION(float);
DECLARE_YAML_CONVERSION(double);
DECLARE_YAML_CONVERSION(long double);
DECLARE_YAML_CONVERSION(YAML::Node);

#undef DECLARE_YAML_CONVERSION

template<> struct conversion<std::string, YAML::Node> {
	static YAML::Node perform(std::string const &);
};

template<> struct conversion<YAML::Node, dr::YamlResult<std::string>> {
	static dr::YamlResult<std::string> perform(YAML::Node const &);
};

template<> struct conversion<std::string_view, YAML::Node> {
	static YAML::Node perform(std::string_view const &);
};

template<> struct conversion<YAML::Node, dr::YamlResult<std::string_view>> {
	static dr::YamlResult<std::string_view> perform(YAML::Node const &);
};

// conversion for std::array
template<typename T, std::size_t N>
struct conversion<YAML::Node, dr::YamlResult<std::array<T, N>>> {
	static constexpr bool possible = dr::can_parse_yaml<T>;

	static dr::YamlResult<std::array<T, N>> perform(YAML::Node const & node) noexcept {
		if (auto error = dr::expectSequence(node, N)) return *error;

		std::array<T, N> result;

		std::size_t index = 0;
		for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
			if (index >= N) return dr::YamlError{"sequence too long, expected " + std::to_string(N) + ", now at index " + std::to_string(index)};
			dr::YamlResult<T> element = dr::parseYaml<T>(*i);
			if (!element) return element.error().appendTrace({std::to_string(index), "", i->Type()});
			result[index++] = std::move(*element);
		}

		return result;
	}
};

template<typename T, std::size_t N>
struct conversion<std::array<T, N>, YAML::Node> {
	static constexpr bool possible = dr::can_encode_yaml<T>;

	static YAML::Node perform(std::array<T, N> const & data) noexcept {
		YAML::Node result;
		for (auto const & value : data) {
			result.push_back(dr::encodeYaml(value));
		}
		return result;
	}
};

// conversion for std::vector
template<typename T>
struct conversion<YAML::Node, dr::YamlResult<std::vector<T>>> {
	static constexpr bool possible = dr::can_parse_yaml<T>;

	static dr::YamlResult<std::vector<T>> perform(YAML::Node const & node) {
		if (node.IsNull()) return std::vector<T>{};
		if (auto error = dr::expectSequence(node)) return *error;

		std::vector<T> result;
		result.reserve(node.size());

		int index = 0;
		for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
			dr::YamlResult<T> element = dr::parseYaml<T>(*i);
			if (!element) return element.error().appendTrace({std::to_string(index), "", i->Type()});
			result.push_back(std::move(*element));
			++index;
		}

		return result;
	}
};

template<typename T>
struct conversion<std::vector<T>, YAML::Node> {
	static constexpr bool possible = dr::can_encode_yaml<T>;

	static YAML::Node perform(std::vector<T> const & data) noexcept {
		YAML::Node result;
		for (auto const & value : data) {
			result.push_back(dr::encodeYaml(value));
		}
		return result;
	}
};

// conversion for std::optional<T>
template<typename T>
struct conversion<YAML::Node, dr::YamlResult<std::optional<T>>> {
	static constexpr bool possible = dr::can_parse_yaml<T>;

	static dr::YamlResult<std::optional<T>> perform(YAML::Node const & node) {
		if (node.IsNull()) return std::optional<T>{};

		dr::YamlResult<T> element = dr::parseYaml<T>(node);
		if (!element) return element.error();
		return {estd::in_place_valid, std::optional<T>{std::move(*element)}};
	}
};

template<typename T>
struct conversion<std::optional<T>, YAML::Node> {
	static constexpr bool possible = dr::can_encode_yaml<T>;

	static YAML::Node perform(std::optional<T> const & data) noexcept {
		// If the optional is false encode it as a boolean
		if (!data) return YAML::Node();
		return dr::encodeYaml(*data);
	}
};

namespace detail {
	// conversion for std::map<Key, Value>
	template<typename Key, typename Value>
	dr::YamlResult<std::map<Key, Value>> parseYamlMap(YAML::Node const & node) {
		if (auto error = dr::expectMap(node)) return *error;

		std::map<Key, Value> result;

		for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
			std::string const & name = i->first.Scalar();

			dr::YamlResult<Key> key = dr::parseYaml<Key>(i->first);
			if (!key) return key.error().appendTrace({name, "", i->first.Type()});

			dr::YamlResult<Value> value = dr::parseYaml<Value>(i->second);
			if (!value) return value.error().appendTrace({name, "", i->second.Type()});

			result.emplace(std::move(*key), std::move(*value));
		}

		return result;
	}
}

// conversion for std::map<std::string, T>
template<typename T>
struct conversion<YAML::Node, dr::YamlResult<std::map<std::string, T>>> {
	static constexpr bool possible = dr::can_parse_yaml<T>;

	static dr::YamlResult<std::map<std::string, T>> perform(YAML::Node const & node) {
		return detail::parseYamlMap<std::string, T>(node);
	}
};

template<typename T>
struct conversion<std::map<std::string, T>, YAML::Node> {
	static constexpr bool possible = dr::can_encode_yaml<T>;

	static YAML::Node perform(std::map<std::string, T> const & map) {
		YAML::Node result;
		for (auto & [key, value] : map) {
			result[key] = dr::encodeYaml(value);
		}
		return result;
	}
};

// conversion for std::map<int, T>
template<typename T>
struct conversion<YAML::Node, dr::YamlResult<std::map<int, T>>> {
	static constexpr bool possible = dr::can_parse_yaml<T>;

	static dr::YamlResult<std::map<int, T>> perform(YAML::Node const & node) {
		return detail::parseYamlMap<int, T>(node);
	}
};

template<typename T>
struct conversion<std::map<int, T>, YAML::Node> {
	static constexpr bool possible = dr::can_encode_yaml<T>;

	static YAML::Node perform(std::map<int, T> const & map) {
		YAML::Node result;
		for (auto & [key, value] : map) {
			result[std::to_string(key)] = dr::encodeYaml(value);
		}
		return result;
	}
};

}
