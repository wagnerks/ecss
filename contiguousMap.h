#pragma once

#include <utility>

namespace ecss {
	template<typename Key, typename Value>
	class ContiguousMap {
	public:
		friend bool operator==(const ContiguousMap& lhs, const ContiguousMap& rhs) {
			return lhs.mSize == rhs.mSize && lhs.mCapacity == rhs.mCapacity && lhs.mData == rhs.mData;
		}

		friend bool operator!=(const ContiguousMap& lhs, const ContiguousMap& rhs) {
			return !(lhs == rhs);
		}

		ContiguousMap(const ContiguousMap& other) : mSize(other.mSize), mCapacity(other.mCapacity) {
			mData = new std::pair<Key, Value>[mCapacity];
			for (auto i = 0; i < mSize; i++) {
				mData[i] = other.mData[i];
			}
		}

		ContiguousMap(ContiguousMap&& other) noexcept : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity) {
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
		~ContiguousMap() { delete[] mData; }

		Value& operator[](Key key) {
			size_t idx = 0;
			const auto pair = search(key, idx);
			if (pair) {
				return pair->second;
			}

			if (mCapacity <= mSize) {
				setCapacity(mCapacity ? mCapacity * 2 : 1);
			}

			mSize++;

			shiftDataRight(idx);

			mData[idx].first = key;

			return mData[idx].second;
		}

		Value& insert(Key key, Value value) {
			size_t idx = 0;
			const auto pair = search(key, idx);
			if (pair) {
				pair->second = std::move(value);
				return pair->second;
			}

			if (mCapacity <= mSize) {
				setCapacity(mCapacity ? mCapacity * 2 : 1);
			}

			mSize++;

			shiftDataRight(idx);

			mData[idx].first = key;
			mData[idx].second = std::move(value);

			return mData[idx].second;
		}

		const std::pair<Key,Value>& find(Key key) {
			size_t idx = 0;
			if (auto pair = search(key, idx)) {
				return *pair;
			}
			return {};
		}

		bool contains(Key key) const {
			size_t idx = 0;
			if (search(key, idx)) {
				return true;
			}

			return false;
		}

		const Value& at(Key key) const {
			size_t idx = 0;
			if (auto pair = search(key, idx)) {
				return pair->second;
			}

			throw std::out_of_range("Key not found");
		}

		class Iterator {
		public:
			using iterator_category = std::random_access_iterator_tag;
			using value_type = std::pair<Key, Value>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator(pointer ptr) : mPtr(ptr) {}

			reference operator*() const { return *mPtr; }
			pointer operator->() const { return mPtr; }
			Iterator& operator++() { ++mPtr; return *this; }
			Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
			Iterator& operator--() { --mPtr; return *this; }
			Iterator operator--(int) { Iterator tmp = *this; --(*this); return tmp; }
			Iterator& operator+=(difference_type n) { mPtr += n; return *this; }
			Iterator& operator-=(difference_type n) { mPtr -= n; return *this; }
			reference operator[](difference_type n) const { return *(mPtr + n); }
			friend bool operator==(const Iterator& a, const Iterator& b) { return a.mPtr == b.mPtr; }
			friend bool operator!=(const Iterator& a, const Iterator& b) { return a.mPtr != b.mPtr; }
			friend bool operator<(const Iterator& a, const Iterator& b) { return a.mPtr < b.mPtr; }
			friend bool operator>(const Iterator& a, const Iterator& b) { return a.mPtr > b.mPtr; }
			friend bool operator<=(const Iterator& a, const Iterator& b) { return a.mPtr <= b.mPtr; }
			friend bool operator>=(const Iterator& a, const Iterator& b) { return a.mPtr >= b.mPtr; }
			difference_type operator-(const Iterator& other) const { return mPtr - other.mPtr; }
		private:
			pointer mPtr;
		};

		Iterator begin() const { return { mData }; }

		Iterator end() const { return { mData + mSize }; }

		void shrinkToFit() { setCapacity(mSize); }

		void reserve(size_t capacity) {	setCapacity(capacity);	}

		size_t size() const { return mSize; }
		std::pair<Key, Value>* data() const { return mData; }

	private:
		void shiftDataRight(size_t begin) {
			if (!mData) {
				return;
			}
			
			for(auto i = mSize - 1; i > begin; i--) {
				mData[i] = std::move(mData[i - 1]);
			}
		}

		void setCapacity(size_t capacity) {
			if (capacity == mCapacity) {
				return;
			}

			if (mCapacity == 0) {
				mCapacity = capacity;
				mData = new std::pair<Key, Value>[mCapacity];
			}
			else {
				mCapacity = capacity;
				std::pair<Key, Value>* newData = new std::pair<Key, Value>[mCapacity];
				for (auto i = 0u; i < mSize; i++) {
					newData[i] = std::move(mData[i]);
				}

				delete[] mData;
				mData = newData;
			}
		}

		std::pair<Key, Value>* search(Key key, size_t& idx) const {
			auto right = mSize;

			if (right == 0 || mData[0].first > key) {
				idx = 0;
				return nullptr;
			}

			if (mData[right - 1].first < key) {
				idx = right;
				return nullptr;
			}

			size_t left = 0u;
			std::pair<Key, Value>* result = nullptr;

			while (true) {
				if (mData[left].first == key) {
					idx = left;
					result = &mData[left];
					break;
				}

				const auto dist = right - left;
				if (dist == 1) {
					idx = left + 1;
					break;
				}

				const auto mid = left + dist / 2;

				if (mData[mid].first > key) {
					right = mid;
					continue;
				}

				if (mData[mid].first == key) {
					idx = mid;
					result = &mData[mid];
					break;
				}

				left = mid;
			}

			return result;
		}

	private:
		std::pair<Key, Value>* mData = nullptr;
		size_t mSize = 0;
		size_t mCapacity = 0;
	};
}
