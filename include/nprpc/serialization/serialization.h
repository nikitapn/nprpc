#pragma once

#include <type_traits>
#include <ostream>
#include <istream>
#include <vector>
#include <string>
#include <array>
#include <cassert>

#include <nprpc/serialization/nvp.hpp>

namespace nprpc::serialization {

class json_iarchive;
class json_oarchive;

template<typename Base, typename Archive, typename Derrived> 
void serialize_base(Archive& ar, Derrived& derrived) {
	static_cast<Base&>(derrived).serialize(ar);
}

template<typename T>
concept JsonArchive = std::is_same_v<T, json_iarchive> || std::is_same_v<T, json_oarchive>;

template<typename Archive, typename T>
void serialize_free(Archive& ar, T& obj) {
	obj.serialize(ar);
}

template<JsonArchive Archive, typename T>
void serialize_free(Archive& ar, T& obj) {
	if (Archive::is_saving::value) {
		ar.bracket_open();
		obj.serialize(ar);
		ar.bracket_close();
	}
}

template<typename Archive>
void serialize_free(Archive& ar, std::string& obj) {
	if constexpr (Archive::is_saving::value) {
		auto size = static_cast<unsigned int>(obj.size());
		ar.save_fundamental(size);
		ar.save_byte_sequence(obj.c_str(), size);
	} else {
		unsigned int size;
		ar.load_fundamental(size);
		obj.resize(size);
		ar.load_byte_sequence(obj.data(), size);
	}
}

template<JsonArchive Archive>
void serialize_free(Archive& ar, std::string& obj) {
	if constexpr (Archive::is_saving::value) {
		ar.save_fundamental('\"');
		ar.save_fundamental(obj);
		ar.save_fundamental('\"');
	} else {
		ar.load_fundamental(obj);
	}
}

template<typename Archive, typename T, typename Alloc>
void serialize_free(Archive& ar, std::vector<T, Alloc>& obj) {
	if constexpr (Archive::is_saving::value) {
		auto size = static_cast<unsigned int>(obj.size());
		ar.save_fundamental(size);
		ar.save_sequence(obj.data(), size);
	} else {
		unsigned int size;
		ar.load_fundamental(size);
		for (unsigned int i = 0; i < size; ++i) {
			obj.emplace_back();
			ar >> obj.back();
		}
	}
}

template<typename Archive, typename T, size_t N>
void serialize_free(Archive& ar, std::array<T, N>& obj) {
	if constexpr (Archive::is_saving::value) {
		auto size = static_cast<unsigned int>(obj.size());
		ar.save_fundamental(size);
		ar.save_sequence(obj.data(), size);
	} else {
		unsigned int size;
		ar.load_fundamental(size);
		for (unsigned int i = 0; i < size; ++i) {
			ar >> obj[i];
		}
	}
}

struct fundamental_t {
	struct save {
		template<typename Archive, typename T>
		static void invoke(Archive& ar, T& obj) {
			ar.save_fundamental(obj);
		}
	};
	struct load {
		template<typename Archive, typename T>
		static void invoke(Archive& ar, T& obj) {
			ar.load_fundamental(obj);
		}
	};
};

struct bool_t {
	struct save {
		template<typename Archive>
		static void invoke(Archive& ar, const bool obj) {
			ar.save_bool(obj);
		}
	};
	struct load {
		template<typename Archive>
		static void invoke(Archive& ar, bool& obj) {
			ar.load_bool(obj);
		}
	};
};


template<typename Archive, size_t N, typename T>
void serialize_nvp(Archive& ar, nvp<N, T>& o) {
	if constexpr (Archive::is_saving::value) {
		if constexpr (std::is_same_v<json_oarchive, Archive>) {
			ar.save_nvp(o);
		} else {
			ar.save(o.obj);
		}
	} else {
		if constexpr (std::is_same_v<json_oarchive, Archive>) {
			ar.load_nvp(o);
		} else {
			ar.load(o.obj);
		}
	}
}

struct nvp_t {
	struct save {
		template<typename Archive, typename T>
		static void invoke(Archive& ar, T& obj) {
			serialize_nvp(ar, obj);
		}
	};
	struct load {
		template<typename Archive, typename T>
		static void invoke(Archive& ar, T& obj) {
			serialize_nvp(ar, obj);
		}
	};
};



class basic_archive_abstract {};

class basic_oarchive_abstract 
	: public basic_archive_abstract {};

class basic_iarchive_abstract 
	: public basic_archive_abstract {};

struct default_t {
	struct save {
		template<typename Archive, typename T>
		static void invoke(Archive& ar, T& obj) {
			serialize_free(ar, obj);
		}
	};
	struct load : save {};
};

template<typename Archive>
class basic_archive {
public:
	Archive* This() noexcept { return static_cast<Archive*>(this); }

	template<typename T>
	void save(T& obj) {
		using type = 
			std::conditional_t<std::is_same_v<T, bool>, bool_t,
			std::conditional_t<std::is_fundamental_v<T>, fundamental_t,
			std::conditional_t<std::is_enum_v<T>, fundamental_t,
			std::conditional_t<std::is_base_of_v<nvp_base, T>, nvp_t,
			default_t
		>>>>;
		type::save::invoke(*This(), obj);
	}

	template<typename T>
	void load(T& obj) {
		using type = 
			std::conditional_t<std::is_fundamental_v<T>, fundamental_t,
			std::conditional_t<std::is_enum_v<T>, fundamental_t,
			std::conditional_t<std::is_base_of_v<nvp_base, T>, nvp_t,
			default_t
		>>>;
		type::load::invoke(*This(), obj);
	}

	template<typename T>
	void save_sequence(const T* data, unsigned int size) {
		for (unsigned int i = 0; i < size; ++i) {
			This()->operator<<(data[i]);
		}
	}
};

template<typename Archive>
class basic_oarchive 
	: public basic_archive<Archive>
	, public basic_oarchive_abstract {
protected:
	std::ostream& os_;
public:
	using is_saving = std::true_type;
	using is_loading = std::false_type;

	Archive* This() noexcept { return static_cast<Archive*>(this); }
	
	template<typename T>
	Archive& operator<<(const T& obj) {
		basic_archive<Archive>::save(const_cast<T&>(obj));
		return *This();
	}

	template<typename T>
	Archive& operator&(const T& obj) {
		return operator<<(obj);
	}

	basic_oarchive(std::ostream& os)
		: os_(os) {}
};

template<typename Archive>
class basic_iarchive 
	: public basic_archive<Archive>
	, public basic_iarchive_abstract {
protected:
	std::istream& is_;
public:
	using is_saving = std::false_type;
	using is_loading = std::true_type;

	Archive* This() noexcept { return static_cast<Archive*>(this); }

	template<typename T>
	Archive& operator>>(T& obj) {	
		basic_archive<Archive>::load(const_cast<T&>(obj));
		return *This();
	}

	template<typename T>
	Archive& operator&(T& obj) {
		return operator>>(obj);
	}

	basic_iarchive(std::istream& is)
		: is_(is) {}
};

} // namespace nprpc::serialization