#pragma once
#include <ostream>

namespace ecss {
	//todo sort and binary search
	template<typename Key, typename Value>
	class ContiguousMap {
	public:
		ContiguousMap(const ContiguousMap& other)
			: mSize(other.mSize),
			mCapacity(other.mCapacity) {
			mData = new std::pair<Key, Value>[mCapacity];
			for (auto i = 0; i < mSize; i++) {
				mData[i] = other.mData[i];
			}
		}
		ContiguousMap(ContiguousMap&& other) noexcept
			: mSize(other.mSize),
			mCapacity(other.mCapacity),
			mData(other.mData) {
			other.mData = nullptr;
		}
		ContiguousMap& operator=(const ContiguousMap& other) {
			if (this == &other)
				return *this;
			mSize = other.mSize;
			mCapacity = other.mCapacity;
			mData = new std::pair<Key, Value>[mCapacity];
			for (auto i = 0; i < mSize; i++) {
				mData[i] = other.mData[i];
			}
			return *this;
		}
		ContiguousMap& operator=(ContiguousMap&& other) noexcept {
			if (this == &other)
				return *this;
			mSize = other.mSize;
			mCapacity = other.mCapacity;
			mData = other.mData;
			other.mData = nullptr;
			return *this;
		}

		ContiguousMap() = default;
		~ContiguousMap() {
			delete[] mData;
		}

		Value& operator[](Key key) {
			for (auto i = 0u; i < mSize; i++) {
				if (mData[i].first == key) {
					return mData[i].second;
				}
			}

			if (mSize >= mCapacity) {
				if (mCapacity == 0) {
					mCapacity = 1;
					mData = new std::pair<Key,Value>[mCapacity];
				}
				else {
					mCapacity *= 2;
					auto newData = new std::pair<Key, Value>[mCapacity];
					for (auto i = 0u; i < mSize; i++) {
						newData[i] = std::move(mData[i]);
					}

					delete[] mData;
					mData = newData;
				}
			}

			mData[mSize].first = key;
			return mData[mSize++].second;
		}

		Value& insert(Key key, Value value) {
			for (auto i = 0u; i < mSize; i++) {
				if (mData[i].first == key) {
					mData[i].second = std::move(value);
					return mData[i].second;
				}
			}

			if (mCapacity <= mSize) {
				if (mCapacity == 0) {
					mCapacity = 1;
					mData = new std::pair<Key, Value>[mCapacity];
				}
				else {
					mCapacity *= 2;
					auto newData = new std::pair<Key, Value>[mCapacity];
					for (auto i = 0u; i < mSize; i++) {
						newData[i] = std::move(mData[i]);
					}

					delete[] mData;
					mData = newData;
				}
			}

			mData[mSize].first = key;
			mData[mSize].second = std::move(value);

			return mData[mSize++].second;
		}

		const std::pair<Key,Value>& find(Key key) {
			for (auto i = 0u; i < mSize; i++) {
				if (mData[i].first == key) {
					return mData[i];
				}
			}

			return {};
		}

		bool contains(Key key) const {
			for (auto i = 0u; i < mSize; i++) {
				if (mData[i].first == key) {
					return true;
				}
			}

			return false;
		}

		Value at(Key key) const {
			for (auto i = 0u; i < mSize; i++) {
				if (mData[i].first == key) {
					return mData[i].second;
				}
			}

			return {};
		}

		class Iterator {
		public:
			std::pair<Key, Value>* ptr;
			Iterator(std::pair<Key, Value>* ptr) : ptr(ptr) {}

			std::pair<Key, Value>& operator*() const {
				return *ptr;
			}

			bool operator!=(Iterator& other) const {
				return ptr != other.ptr;
			}

			Iterator& operator++() {
				return ++ptr, *this;
			}
		};

		Iterator begin() const {
			return { mData };
		}

		Iterator end() const {
			return { mData + mSize };
		}

		void shrinkToFit() {
			if (mCapacity == mSize) {
				return;
			}

			mCapacity = mSize;

			auto newData = new std::pair<Key, Value>[mCapacity];
			for (auto i = 0u; i < mSize; i++) {
				newData[i] = std::move(mData[i]);
			}

			delete[] mData;
			mData = newData;
		}

	private:
		size_t mSize = 0;
		size_t mCapacity = 0;
		std::pair<Key,Value>* mData = nullptr;
	};
}
