#pragma once

#include <exception>
#include <hsa.h>
#include <string>

namespace rts { namespace hsa {

class HsaException: public std::exception {
public:
	/// C'tor (C-String error message)
	explicit HsaException(const char* message) : message(message) { }

	/// C'tor (C++ STL string error message)
	explicit HsaException(const std::string& message) :	message(message) { }

	/// Destructor
	virtual ~HsaException() throw () { }

	/// Returns a pointer to the (constant) error message.
	virtual const char* what() const throw () {
		return message.c_str();
	}

protected:
	/// The error message.
	std::string message;

};

}}
