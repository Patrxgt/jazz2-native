#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../nCine/Base/HashMap.h"

#include <angelscript.h>

#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Scripting
{
	class CScriptArray;
	class CScriptDictionary;
	class CScriptDictValue;

	typedef String dictKey_t;
	typedef HashMap<dictKey_t, CScriptDictValue> dictMap_t;

	/** @brief **AngelScript** dictionary value */
	class CScriptDictValue
	{
		friend class CScriptDictionary;

	public:
		// This class must not be declared as local variable in C++, because it needs
		// to receive the script engine pointer in all operations. The engine pointer
		// is not kept as member in order to keep the size down
		CScriptDictValue();
		CScriptDictValue(asIScriptEngine* engine, void* value, std::int32_t typeId);

		// Destructor must not be called without first calling FreeValue, otherwise a memory leak will occur
		~CScriptDictValue();

		// Replace the stored value
		void Set(asIScriptEngine* engine, void* value, std::int32_t typeId);
		void Set(asIScriptEngine* engine, const std::int64_t& value);
		void Set(asIScriptEngine* engine, const double& value);
		void Set(asIScriptEngine* engine, CScriptDictValue& value);

		// Gets the stored value. Returns false if the value isn't compatible with the informed typeId
		bool Get(asIScriptEngine* engine, void* value, std::int32_t typeId) const;
		bool Get(asIScriptEngine* engine, std::int64_t& value) const;
		bool Get(asIScriptEngine* engine, double& value) const;

		// Returns the address of the stored value for inspection
		const void* GetAddressOfValue() const;

		// Returns the type id of the stored value
		std::int32_t GetTypeId() const;

		// Free the stored value
		void FreeValue(asIScriptEngine* engine);

		// GC callback
		void EnumReferences(asIScriptEngine* engine);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		union
		{
			asINT64 m_valueInt;
			double  m_valueFlt;
			void* m_valueObj;
		};
		std::int32_t m_typeId;
#endif
	};

	/** @brief **AngelScript** dictionary */
	class CScriptDictionary
	{
	public:
		// Factory functions
		static CScriptDictionary* Create(asIScriptEngine* engine);

		// Called from the script to instantiate a dictionary from an initialization list
		static CScriptDictionary* Create(asBYTE* buffer);

		// Reference counting
		void AddRef() const;
		void Release() const;

		// Reassign the dictionary
		CScriptDictionary& operator=(const CScriptDictionary& other);

		// Sets a key/value pair
		void Set(const dictKey_t& key, void* value, std::int32_t typeId);
		void Set(const dictKey_t& key, const std::int64_t& value);
		void Set(const dictKey_t& key, const double& value);

		// Gets the stored value. Returns false if the value isn't compatible with the informed typeId
		bool Get(const dictKey_t& key, void* value, std::int32_t typeId) const;
		bool Get(const dictKey_t& key, std::int64_t& value) const;
		bool Get(const dictKey_t& key, double& value) const;

		// Index accessors. If the dictionary is not const it inserts the value if it doesn't already exist
		// If the dictionary is const then a script exception is set if it doesn't exist and a null pointer is returned
		CScriptDictValue* operator[](const dictKey_t& key);
		const CScriptDictValue* operator[](const dictKey_t& key) const;

		// Returns the type id of the stored value, or negative if it doesn't exist
		std::int32_t GetTypeId(const dictKey_t& key) const;

		// Returns true if the key is set
		bool Exists(const dictKey_t& key) const;

		// Returns true if there are no key/value pairs in the dictionary
		bool IsEmpty() const;

		// Returns the number of key/value pairs in the dictionary
		asUINT GetSize() const;

		// Deletes the key
		bool Delete(const dictKey_t& key);

		// Deletes all keys
		void DeleteAll();

		// Get an array of all keys
		CScriptArray* GetKeys() const;

		/** @brief **AngelScript** dictionary iterator */
		class CIterator
		{
			friend class CScriptDictionary;

		public:
			void operator++();    // Pre-increment
			void operator++(int); // Post-increment

			// This is needed to support C++11 range-for
			CIterator& operator*();

			bool operator==(const CIterator& other) const;
			bool operator!=(const CIterator& other) const;

			// Accessors
			const dictKey_t& GetKey() const;
			std::int32_t GetTypeId() const;
			bool GetValue(std::int64_t& value) const;
			bool GetValue(double& value) const;
			bool GetValue(void* value, std::int32_t typeId) const;
			const void* GetAddressOfValue() const;

		protected:
			CIterator();
			CIterator(const CScriptDictionary& dict, dictMap_t::const_iterator it);

			CIterator& operator=(const CIterator&) { return *this; } // Not used

#ifndef DOXYGEN_GENERATING_OUTPUT
			dictMap_t::const_iterator m_it;
			const CScriptDictionary& m_dict;
#endif
		};

		CIterator begin() const;
		CIterator end() const;
		CIterator find(const dictKey_t& key) const;

		// Garbage collections behaviours
		std::int32_t GetRefCount();
		void SetGCFlag();
		bool GetGCFlag();
		void EnumReferences(asIScriptEngine* engine);
		void ReleaseAllReferences(asIScriptEngine* engine);

	protected:
		// Since the dictionary uses the asAllocMem and asFreeMem functions to allocate memory
		// the constructors are made protected so that the application cannot allocate it
		// manually in a different way
		CScriptDictionary(asIScriptEngine* engine);
		CScriptDictionary(asBYTE* buffer);

		// We don't want anyone to call the destructor directly, it should be called through the Release method
		virtual ~CScriptDictionary();

		// Cache the object types needed
		void Init(asIScriptEngine* engine);

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Our properties
		asIScriptEngine* engine;
		mutable std::int32_t refCount;
		mutable bool gcFlag;
		dictMap_t dict;
#endif
	};

	/** @relatesalso CScriptDictionary
		@brief Registers `dictionary` type to **AngelScript** engine
	*/
	void RegisterDictionary(asIScriptEngine* engine);
}

#endif
