#pragma once
#include <dr_error/error_or.hpp>

#include <yaml-cpp/yaml.h>
#include <estd/convert/convert.hpp>

#include <algorithm>
#include <array>
#include <locale>
#include <stdexcept>
#include <string>

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
using YamlResult = dr::result<T, YamlError>;

// Conversion tag for YAML parsers.
template<typename T> struct ParseYaml {
	using type = dr::result<T, YamlError>;
};

/// Trait to check if a type can be converted from a YAML::Node.
template<typename T>
constexpr bool can_parse_yaml = estd::can_convert_tagged_v<YAML::Node const &, ParseYaml<T>>;

/// Parse a YAML::Node into a type T.
template<typename T>
YamlResult<T> parseYaml(YAML::Node const & node) {
	static_assert(can_parse_yaml<T>, "No yaml conversion defined for T");
	return convert(node, ParseYaml<T>{});
}

DetailedError expectMap(YAML::Node const & node);
DetailedError expectMap(YAML::Node const & node, std::size_t size);
DetailedError expectSequence(YAML::Node const & node);
DetailedError expectSequence(YAML::Node const & node, std::size_t size);
DetailedError expectScalar(YAML::Node const & node);

std::string toString(YAML::NodeType::value);

template<typename T>
void setIfExists(T & output, YAML::Node const & node, std::string const & key) {
	if (node[key]) output = node[key].as<T>();
}

ErrorOr<YAML::Node> readYamlFile(std::string const & path);

template<typename T>
ErrorOr<T> convertChild(YAML::Node const & node, std::string const & key) {
	if (!node[key]) return DetailedError{std::errc::invalid_argument, "no such key: " + key};
	try {
		return node[key].as<T>();
	} catch (std::system_error const & e) {
		return DetailedError{e.code(), std::string{"failed to convert node: "} + e.what()};
	} catch (std::exception const & e) {
		return DetailedError{std::errc::invalid_argument, std::string{"failed to convert node: "} + e.what()};
	}
}

}

namespace YAML {
	template<typename T, std::size_t N>
	struct convert<std::array<T, N>> {
		static Node encode(std::array<T, N> const & array) {
			Node result;
			for (T const & val : array) result.push_back(Node{val});
			return result;
		}
		static bool decode(Node const & node, std::array<T, N> & result) {
			if (!node.IsSequence()) return false;
			if (node.size() != N) return false;
			for (std::size_t i = 0; i < N; ++i) {
				if (!convert<T>::decode(node[i], result[i])) return false;
			}
			return true;
		}
	};
}

namespace dr {

// conversions for primitives
YamlResult<std::string> convert(YAML::Node const & node, ParseYaml<std::string>);
YamlResult<bool>        convert(YAML::Node const & node, ParseYaml<bool>);

YamlResult<char>      convert(YAML::Node const & node, ParseYaml<char>);
YamlResult<short>     convert(YAML::Node const & node, ParseYaml<short>);
YamlResult<int>       convert(YAML::Node const & node, ParseYaml<int>);
YamlResult<long>      convert(YAML::Node const & node, ParseYaml<long>);
YamlResult<long long> convert(YAML::Node const & node, ParseYaml<long long>);

YamlResult<unsigned char>      convert(YAML::Node const & node, ParseYaml<unsigned char>);
YamlResult<unsigned short>     convert(YAML::Node const & node, ParseYaml<unsigned short>);
YamlResult<unsigned int>       convert(YAML::Node const & node, ParseYaml<unsigned int>);
YamlResult<unsigned long>      convert(YAML::Node const & node, ParseYaml<unsigned long>);
YamlResult<unsigned long long> convert(YAML::Node const & node, ParseYaml<unsigned long long>);

YamlResult<float>       convert(YAML::Node const & node, ParseYaml<float>);
YamlResult<double>      convert(YAML::Node const & node, ParseYaml<double>);
YamlResult<long double> convert(YAML::Node const & node, ParseYaml<long double>);

// conversion for std::array
template<typename T, std::size_t N>
std::enable_if_t<can_parse_yaml<T>, YamlResult<std::array<T, N>>>
convert(YAML::Node const & node, ParseYaml<std::array<T, N>>) {
	if (!node.IsSequence()) return YamlError{"unexpected node type, expected sequence, got " + toString(node.Type())};
	if (node.size() != N)   return YamlError{"wrong number of elements, expected " + std::to_string(N) + ", got " + std::to_string(node.size())};

	std::array<T, N> result;

	int index = 0;
	for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
		if (index >= N) return YamlError{"sequence too long, expected " + std::to_string(N) + ", now at index " + std::to_string(index)};
		YamlResult<T> element = parseYaml<T>(*i);
		if (!element) return element.error().appendTrace({std::to_string(index), "", i->Type()});
		result[index++] = std::move(*element);
	}

	return result;
}

// conversion for std::vector
template<typename T>
std::enable_if_t<can_parse_yaml<T>, YamlResult<std::vector<T>>>
convert(YAML::Node const & node, ParseYaml<std::vector<T>>) {
	if (node.IsNull()) return std::vector<T>{};
	if (!node.IsSequence()) return YamlError{"unexpected node type, expected sequence, got " + toString(node.Type())};

	std::vector<T> result;
	result.reserve(node.size());

	int index = 0;
	for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
		YamlResult<T> element = parseYaml<T>(*i);
		if (!element) return element.error().appendTrace({std::to_string(index), "", i->Type()});
		result.push_back(std::move(*element));
		++index;
	}

	return result;
}

// conversion for std::map<std::string, T>
template<typename T>
std::enable_if_t<can_parse_yaml<T>, YamlResult<std::map<std::string, T>>>
convert(YAML::Node const & node, YamlResult<std::map<std::string, T>>) {
	if (!node.IsMap()) return YamlError{"unexpected node type, expected map, got " + toString(node.Type())};

	std::map<std::string, T> result;

	for (YAML::const_iterator i = node.begin(); i != node.end(); ++i) {
		std::string const & name = i->first.Scalar();
		YamlResult<T> element = parseYaml<T>(i->second);
		if (!element) return element.error().appendTrace({name, "", i->second.Type()});
		result.insert(name, std::move(*element));
	}

	return result;
}

}
