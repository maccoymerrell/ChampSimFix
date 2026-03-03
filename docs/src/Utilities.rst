.. _Utilities:

=====================================
Utility Libraries
=====================================

ChampSim provides utility libraries for common microarchitecture simulation tasks.

----------------------------------
MSL (Microarchitecture Simulation Library)
----------------------------------

The MSL library provides data structures and utilities specifically designed for microarchitecture simulation.

LRU Table
~~~~~~~~~

The ``champsim::msl::lru_table`` provides an efficient set-associative table with LRU replacement.

.. doxygenclass:: champsim::msl::lru_table
   :members:

Usage Example
^^^^^^^^^^^^^

.. code-block:: cpp

   // Create a 256-set, 4-way LRU table for tracking stride information
   champsim::msl::lru_table<uint64_t, stride_info> table{256, 4};

   // Check for hit and get iterator
   auto [hit, entry] = table.check_hit(address);

   if (hit) {
       // Update existing entry
       entry->data.stride = new_stride;
   } else {
       // Insert new entry with eviction
       auto evicted = table.insert(address, stride_info{});
   }

Forward Counters
~~~~~~~~~~~~~~~~

The ``champsim::msl::fwcounter`` provides fixed-width saturating counters.

.. doxygenclass:: champsim::msl::fwcounter
   :members:

Usage Example
^^^^^^^^^^^^^

.. code-block:: cpp

   // 2-bit saturating counter (0-3)
   champsim::msl::fwcounter<2> counter{1}; // Initialize to 1

   counter++; // Increment (now 2)
   counter++; // Increment (now 3)
   counter++; // Saturates at 3

   counter--; // Decrement (now 2)

Common uses:

* Branch prediction confidence (2-bit saturating counters)
* Re-reference prediction (RRIP values)
* Access counting with saturation

Bit Manipulation
~~~~~~~~~~~~~~~~

The ``champsim::msl::bits`` namespace provides bit-level operations.

Functions include:

* Bit extraction and insertion
* Population count (number of 1 bits)
* Leading/trailing zero count
* Bit rotation
* Power-of-two checks

----------------------------------
Generic Utilities
----------------------------------

The ``util/`` directory contains general-purpose utilities.

Bit Operations
~~~~~~~~~~~~~~

.. doxygennamespace:: champsim::bits
   :members:

Provides low-level bit manipulation:

* ``extract_bits(value, low, high)``: Extract bit range
* ``set_bits(value, low, high, new_val)``: Set bit range
* ``popcount(value)``: Count set bits

Algorithm Utilities
~~~~~~~~~~~~~~~~~~~

.. doxygennamespace:: champsim::algorithm
   :members:

STL-like algorithms adapted for simulation:

* Range operations
* Container transformations
* Sorting and searching

Bit Enums
~~~~~~~~~

.. doxygennamespace:: champsim::bit_enum
   :members:

Support for enum flags:

.. code-block:: cpp

   enum class Flags : uint8_t {
       DIRTY = 0x01,
       VALID = 0x02,
       PREFETCH = 0x04
   };

   // Enable bitwise operations
   Flags combined = Flags::DIRTY | Flags::VALID;

Type Traits
~~~~~~~~~~~

.. doxygennamespace:: champsim::type_traits
   :members:

Compile-time type inspection:

* SFINAE detection helpers
* Template metaprogramming utilities
* Concept emulation for C++14/17

Ratio Utilities
~~~~~~~~~~~~~~~

.. doxygennamespace:: champsim::ratio
   :members:

Provides compile-time ratios:

* ``kibi``: 2^10 (1024)
* ``mebi``: 2^20 (1048576)
* ``gibi``: 2^30 (1073741824)
* ``tebi``: 2^40 (1099511627776)

Usage Example
^^^^^^^^^^^^^

.. code-block:: cpp

   constexpr auto cache_size = 256 * champsim::kibi; // 256 KB
   constexpr auto memory_size = 4 * champsim::gibi;   // 4 GB

Units
~~~~~

.. doxygennamespace:: champsim::units
   :members:

Type-safe unit conversions:

* Time units (picoseconds, nanoseconds, cycles)
* Size units (bytes, kilobytes, etc.)
* Bandwidth units (bytes/cycle, GB/s)

Span
~~~~

.. doxygenclass:: champsim::span
   :members:

Non-owning view over contiguous sequences:

.. code-block:: cpp

   std::vector<int> vec = {1, 2, 3, 4, 5};
   champsim::span<int> view{vec};

   // Access without copying
   for (auto& elem : view) {
       elem *= 2;
   }

To Underlying
~~~~~~~~~~~~~

Converts enum values to their underlying integer type:

.. code-block:: cpp

   enum class Color { RED = 1, GREEN = 2, BLUE = 3 };

   auto value = champsim::to_underlying(Color::RED); // Returns 1

Detect Utilities
~~~~~~~~~~~~~~~~

The ``detect.h`` header provides SFINAE-based detection idioms.

.. doxygennamespace:: champsim::detect
   :members:

Usage Example
^^^^^^^^^^^^^

.. code-block:: cpp

   // Detect if a type has a specific member function
   template <typename T>
   using has_initialize = decltype(std::declval<T>().initialize());

   template <typename T>
   constexpr bool has_initialize_v = is_detected_v<has_initialize, T>;

This is used internally to detect which module interface functions are implemented.
