#pragma once

#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>


template <typename T>
class RawMemory
{
	public:
		RawMemory() = default;

		explicit RawMemory(size_t capacity)
			: buffer_(Allocate(capacity))
			, capacity_(capacity)
		{
		}

		~RawMemory()
		{
			Deallocate(buffer_);
		}

		T* operator+(size_t offset) noexcept
		{
			// Разрешаем получать адрес ячейки памяти за последним эл-м массива
			//assert(offset <= capacity_); // для отладки
			return buffer_ + offset;
		}

		const T* operator+(size_t offset) const noexcept
		{
			return const_cast<RawMemory&>(*this) + offset;
		}

		const T& operator[](size_t index) const noexcept
		{
			return const_cast<RawMemory&>(*this)[index];
		}

		T& operator[](size_t index) noexcept
		{
			assert(index < capacity_);
			return buffer_[index];
		}

		void Swap(RawMemory& other) noexcept
		{
			std::swap(buffer_, other.buffer_);
			std::swap(capacity_, other.capacity_);
		}

		const T* GetAddress() const noexcept
		{
			return buffer_;
		}

		T* GetAddress() noexcept
		{
			return buffer_;
		}

		size_t Capacity() const
		{
			return capacity_;
		}

	private:
		// Выделяем сырую память под N элементов и возвращ указатель на неё
		static T* Allocate(size_t n)
		{
			return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
		}

		// Освобождаем сырую память
		static void Deallocate(T* buf) noexcept
		{
			operator delete(buf);
		}

		T* buffer_ = nullptr;
		size_t capacity_ = 0;
};


template <typename T>
class Vector
{
	public:
		using iterator = T*;
		using const_iterator = const T*;

		iterator begin() noexcept
		{
			return data_.GetAddress();
		}

		iterator end()   noexcept
		{
			return data_.GetAddress() + size_;
		}

		const_iterator begin() const noexcept
		{
			return data_.GetAddress();
		}

		const_iterator end()   const noexcept
		{
			return data_.GetAddress() + size_;
		}

		const_iterator cbegin() const noexcept
		{
			return begin();
		}

		const_iterator cend()   const noexcept
		{
			return end();
		}

		Vector() = default;

		~Vector()
		{
			std::destroy_n(data_.GetAddress(), size_);
		}

		explicit Vector(size_t size)
			: data_(size)
			, size_(size)   //
		{
			std::uninitialized_value_construct_n(data_.GetAddress(), size);
		}

		Vector(const Vector& other)
			: data_(other.size_)
			, size_(other.size_)   //
		{
			std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
		}
	
		template <typename... Args>
		iterator Emplace(const_iterator pos, Args&&... args) // следуем презе
		{
			const size_t offset = pos - begin();

			if (size_ <  Capacity())
			{
				if (pos == end())   // zapis v posledniy, если pos == end(), то создаем элемент в неинициализированной памяти с помощью new или std::construct_at.
				{
					// std::construct_at(pos, T(std::forward<Args>(args)...));  // эта зараза внутри себя дважды МОВЕт - подножка, однако 
					new (data_ + offset) T(std::forward<Args>(args)...);        // а эта зараза - всего один раз
				}
				else
				{
					/*alignas(A) char buf[sizeof(A)];  
					  A* obj = std::construct_at<A>(reinterpret_cast<A*>(buf), 42, "abc", vector<double>{1, 2, 3});*/
					alignas(T) char buf[sizeof(T)];
					T* tmp_obj = std::construct_at<T>(reinterpret_cast<T*>(buf), std::forward<Args>(args)...); //создаем временный объект тут, а то вдруг икслючение

					// последний элемент сдвигаем вправо на одну позицию
					new (data_ + size_) T(std::move(data_[size_ - 1]));
					// с помощью move_backward сдвигаем оставшиеся элементы вправо
					std::move_backward(begin() + offset, end() - 1, end());
					//в pos перемещаем временный объект.
					data_[offset] = std::move(*tmp_obj);

                    std::destroy_at(tmp_obj);
				}
			}
			else
			{
				// - выделяем новую память;
				const size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
				RawMemory<T> new_data(new_capacity);

				//- создаем в ней вставляемый элемент;
				new (new_data + offset) T(std::forward<Args>(args)...);

				// переместить/копировать объекты до вставляемого и после элемента в новую память;
				if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) // если МОВется или НЕ копируется, то МОВем 
				{
					// перемещаемо или НЕ копируемо - перемещ
					std::uninitialized_move_n(data_.GetAddress(), offset, new_data.GetAddress());
					std::uninitialized_move_n(data_.GetAddress() + offset, size_ - offset, new_data.GetAddress() + offset + 1);
				}
				else
				{
                    // НЕ перемещаемо и копируемо - копируем
                    std::uninitialized_copy_n(data_.GetAddress(), offset, new_data.GetAddress());
					std::uninitialized_copy_n(data_.GetAddress() + offset, size_ - offset, new_data.GetAddress() + offset + 1);
				}

				// вызываем деструкторы у объектов в старой памяти;
				std::destroy_n(data_.GetAddress(), size_);
				data_.Swap(new_data); // вызываем метод swap.
			}

			++size_;
			return begin() + offset;
		}

		iterator Insert(const_iterator pos, const T& value)
		{
			return Emplace(pos, value);
		}

		iterator Insert(const_iterator pos, T&& value)
		{
			return Emplace(pos, std::move(value));
		}

		iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>)
		{
			const size_t offset = pos - begin();
			std::move(begin() + offset + 1, end(), begin() + offset);
			std::destroy_at(data_ + (size_ - 1));
			--size_;
			return begin() + offset;
		}

        template <typename... Args>
        T& EmplaceBack(Args&&... args) //преза
        {
            return *Emplace(data_ + size_, std::forward<Args>(args)...);
        }

		void Swap(Vector& other) noexcept
		{
			if (&data_ == &other.data_)
			{
				return;
			}

			data_.Swap(other.data_);
			std::swap(size_, other.size_);
		}

		Vector(Vector&& other) noexcept
		{
			data_.Swap(other.data_); // двойной свайп 
			std::swap(size_, other.size_);
		}

		Vector& operator=(const Vector& rhs)
		{
			if (this == &rhs)
			{
				return *this;
			}

			if (rhs.size_ > data_.Capacity())
			{
				Vector rhs_copy(rhs);
				Swap(rhs_copy);
			}
			else
			{
				// Если тек размер больше - уничтожаем лишнее
				if (size_ > rhs.size_)
				{
					std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
				}

				if (rhs.size_ == 0)
				{
					size_ = rhs.size_;
					return *this;
				}

				// Копируем элементы в сущ место
				if (size_ >= rhs.size_)
				{
					std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + rhs.size_, data_.GetAddress());
				}
				else
				{
					std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_, data_.GetAddress());
					std::uninitialized_copy(rhs.data_.GetAddress() + size_, rhs.data_.GetAddress() + rhs.size_, data_.GetAddress() + size_);
				}
				size_ = rhs.size_;
			}
			return *this;
		}

		Vector& operator=(Vector&& rhs) noexcept
		{
			if (this != &rhs)
			{
				Swap(rhs);
			}
			return *this;
		}

		void Reserve(size_t new_capacity)
		{
			if (new_capacity <= data_.Capacity())
			{
				return;
			}
			RawMemory<T> new_data(new_capacity);

			// constexpr оператор if вычислится во время компиляции!!!!
			if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
			{
				std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
			}
			else
			{
				std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
			}

			std::destroy_n(data_.GetAddress(), size_);
			data_.Swap(new_data);
		}

		void Resize(size_t new_size)
		{
			if (new_size < size_)
			{
				// Уменьшаем размер - уничтожаем лишнее
				std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
			}
			else if (new_size > size_)
			{
				// Увеличиваем размер
				Reserve(new_size);
				std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
			}
			size_ = new_size;
		}

		void PushBack(const T& value)
		{
			if (size_ == Capacity())
			{
				// Нужно увеличить вместимость
				const size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
				RawMemory<T> new_data(new_capacity);

				// Переносим/копируем элементы в новое хранилище
				if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
				{
					std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
				}
				else
				{
					std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
				}

				// Создаем копию элемента в конце
				new (new_data + size_) T(value);

				// Уничтожаем старые элементы и переключаем буфер
				std::destroy_n(data_.GetAddress(), size_);
				data_.Swap(new_data);
			}
			else
			{
				// Место есть - просто создаем копию элемента
				new (data_ + size_) T(value);
			}
			++size_;
		}

		void PushBack(T&& value)
		{
			if (size_ == Capacity())
			{
				// Нужно увеличить вместимость
				const size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
				RawMemory<T> new_data(new_capacity);

				// Переносим/копируем элементы в новое хранилище
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
				{
					std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
				}
				else
				{
					std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
				}

				// Перемещаем элемент в конец
				new (new_data + size_) T(std::move(value));

				// Уничтожаем старые элементы и переключаем буфер
				std::destroy_n(data_.GetAddress(), size_);
				data_.Swap(new_data);
			}
			else
			{
				// Место есть - просто перемещаем элемент
				new (data_ + size_) T(std::move(value));
			}
			++size_;
		}

		void PopBack() noexcept
		{
			assert(size_ > 0);
			--size_;
			std::destroy_at(data_.GetAddress() + size_);
		}

		size_t Size() const noexcept
		{
			return size_;
		}

		size_t Capacity() const noexcept
		{
			return data_.Capacity();
		}

		const T& operator[](size_t index) const noexcept
		{
			return const_cast<Vector&>(*this)[index];
		}

		T& operator[](size_t index) noexcept
		{
			assert(index < size_);
			return data_[index];
		}

	private:
		RawMemory<T> data_;
		size_t size_ = 0;

		// Выделяет сырую память под n элементов и возвращает указатель на неё
		static T* Allocate(size_t n)
		{
			return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
		}

		// Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
		static void Deallocate(T* buf) noexcept
		{
			operator delete(buf);
		}

		static void DestroyN(T* buf, size_t n) noexcept
		{
			for (size_t i = 0; i != n; ++i)
			{
				Destroy(buf + i);
			}
		}

		// Создаёт копию объекта elem в сырой памяти по адресу buf
		static void CopyConstruct(T* buf, const T& elem)
		{
			new (buf) T(elem);
		}

		// Вызывает деструктор объекта по адресу buf
		static void Destroy(T* buf) noexcept
		{
			buf->~T();
		}
}; // my
