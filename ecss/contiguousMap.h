#pragma once

#include <utility>

namespace ecss {
	template<typename Key, typename Value>
	class ContiguousMap {
	public:
		friend bool operator==(const ContiguousMap& lhs, const ContiguousMap& rhs) noexcept {
			return lhs.mSize == rhs.mSize && lhs.mCapacity == rhs.mCapacity && lhs.mData == rhs.mData;
		}

		friend bool operator!=(const ContiguousMap& lhs, const ContiguousMap& rhs) noexcept {
			return !(lhs == rhs);
		}

		ContiguousMap(const ContiguousMap& other) noexcept : mSize(other.mSize), mCapacity(other.mCapacity) {
			mData = new std::pair<Key, Value>[mCapacity];
			for (auto i = 0; i < mSize; i++) {
				mData[i] = other.mData[i];
			}
		}

		ContiguousMap(ContiguousMap&& other) noexcept : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity) {
			other.mData = nullptr;
		}

		ContiguousMap& operator=(const ContiguousMap& other) noexcept {
			if (this == &other)
				return *this;

			delete[] mData;
			mSize = other.mSize;
			mCapacity = other.mCapacity;
			mData = new std::pair<Key, Value>[other.mCapacity];
			assert(other.mSize <= other.mCapacity);
			for (auto i = 0u; i < other.mSize; i++) {
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

		ContiguousMap() noexcept = default;
		~ContiguousMap() noexcept { delete[] mData; }

		Value& operator[](Key key) noexcept {
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

		Value& insert(Key key, Value value) noexcept {
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

		const std::pair<Key,Value>& find(Key key) noexcept {
			size_t idx = 0;
			if (auto pair = search(key, idx)) {
				return *pair;
			}
			return {};
		}

		bool contains(Key key) const noexcept {
			size_t idx = 0;
			if (search(key, idx)) {
				return true;
			}

			return false;
		}

		const Value& at(Key key) const noexcept {
			size_t idx = 0;
			if (auto pair = search(key, idx)) {
				return pair->second;
			}
			static Value dummy;
			assert(false);
			return dummy;
		}

		class Iterator {
		public:
			using iterator_category = std::random_access_iterator_tag;
			using value_type = std::pair<Key, Value>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator(pointer ptr) noexcept : mPtr(ptr) {}

			reference operator*() const noexcept { return *mPtr; }
			pointer operator->() const noexcept { return mPtr; }
			Iterator& operator++() noexcept { ++mPtr; return *this; }
			Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }
			Iterator& operator--() noexcept { --mPtr; return *this; }
			Iterator operator--(int) noexcept { Iterator tmp = *this; --(*this); return tmp; }
			Iterator& operator+=(difference_type n) noexcept { mPtr += n; return *this; }
			Iterator& operator-=(difference_type n) noexcept { mPtr -= n; return *this; }
			reference operator[](difference_type n) const noexcept { return *(mPtr + n); }
			friend bool operator==(const Iterator& a, const Iterator& b) noexcept { return a.mPtr == b.mPtr; }
			friend bool operator!=(const Iterator& a, const Iterator& b) noexcept { return a.mPtr != b.mPtr; }
			friend bool operator<(const Iterator& a, const Iterator& b) noexcept { return a.mPtr < b.mPtr; }
			friend bool operator>(const Iterator& a, const Iterator& b) noexcept { return a.mPtr > b.mPtr; }
			friend bool operator<=(const Iterator& a, const Iterator& b) noexcept { return a.mPtr <= b.mPtr; }
			friend bool operator>=(const Iterator& a, const Iterator& b) noexcept { return a.mPtr >= b.mPtr; }
			difference_type operator-(const Iterator& other) const noexcept { return mPtr - other.mPtr; }
		private:
			pointer mPtr;
		};

		Iterator begin() const noexcept { return { mData }; }

		Iterator end() const noexcept { return { mData + mSize }; }

		void shrinkToFit() noexcept { setCapacity(mSize); }

		void reserve(size_t capacity) noexcept { setCapacity(capacity);	}

		size_t size() const noexcept { return mSize; }
		std::pair<Key, Value>* data() const noexcept { return mData; }

	private:
		void shiftDataRight(size_t begin) noexcept {
			if (!mData) {
				return;
			}
			
			for(auto i = mSize - 1; i > begin; i--) {
				mData[i] = std::move(mData[i - 1]);
			}
		}

		void setCapacity(size_t capacity) noexcept {
			if (capacity == mCapacity) {
				return;
			}
			capacity = std::max(capacity, mSize);

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

		std::pair<Key, Value>* search(const Key& key, size_t& idx) const noexcept {
			const size_t n = mSize;
			auto* p = mData;
			for (size_t i = 0; i < n; ++i, ++p) {
				if (p->first == key) { idx = i; return const_cast<std::pair<Key, Value>*>(p); }
				if (p->first > key) { idx = i; return nullptr; }
			}
			idx = n;
			return nullptr;
		}

	private:
		std::pair<Key, Value>* mData = nullptr;
		size_t mSize = 0;
		size_t mCapacity = 0;
	};
}
