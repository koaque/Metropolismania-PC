// The platform layer, Metrowerks C++ runtime: operator new and delete, array construction, the chain of global objects and the run of
// the static constructors, written from what they do and proven against the recompiled functions by the difftest (plan P7).
// Prototypes are in config/platform_signatures.txt. The original's exception paths (a failed allocation that throws, a constructor
// that throws) are never reached by the game; here they fault loudly instead.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kNewHandler = 0x001AB200u;     // the function operator new calls when malloc fails (0 = none)
        constexpr uint32_t kNewThrows = 0x001AB208u;      // a byte: non-zero makes a failed allocation throw, else operator new returns 0
        constexpr uint32_t kGlobalChain = 0x001BDFC8u;    // head of the chain of registered global objects (next +0, destructor +4, object +8)
        constexpr uint32_t kStaticInitFirst = 0x001BCDD0u; // the table of static constructors, as bounds
        constexpr uint32_t kStaticInitLast = 0x001BCDE0u;

        // Runs `constructor(element, 1)` over `count` elements of `size` bytes from `first`, as the runtime's array constructors do.
        void constructElements(uint32_t first, uint32_t constructor, uint32_t size, uint32_t count)
        {
            uint32_t element = first;
            for (uint32_t i = 0; i < count; ++i, element += size)
                port::callGuest<void>(constructor, element, 1);
        }
    }

    // operator new: a size of 0 asks for 1 byte; while malloc fails the new handler (if there is one) runs and the allocation is tried
    // again; without a handler it returns 0, or throws when the throw flag is set.
    GuestPtr<void> sdk_operator_new(uint32_t size)
    {
        if (size == 0)
            size = 1;
        for (;;)
        {
            if (const uint32_t block = sdk::malloc(size))
                return GuestPtr<void>{block};
            if (const uint32_t handler = port::load<uint32_t>(kNewHandler))
            {
                port::callGuest<void>(handler);
                continue;
            }
            if (port::load<int8_t>(kNewThrows) == 0)
                return GuestPtr<void>{};
            port::fatal("operator new: out of memory, and the original would throw bad_alloc", addr::sdk_operator_new);
        }
    }

    // operator delete: frees the block.
    void sdk_operator_delete(GuestPtr<void> block) { sdk::free(block.address); }

    // The compiler's array new and delete are operator new and delete.
    GuestPtr<void> sdk_mw_NewArray(uint32_t size) { return sdk_operator_new(size); }
    void sdk_mw_DeleteArray(GuestPtr<void> block) { sdk_operator_delete(block); }

    // Constructs an array in place: the constructor runs on every element with 1 as its second argument.
    void sdk_construct_array(GuestPtr<void> array, GuestPtr<void> constructor, GuestPtr<void> destructor, uint32_t size, uint32_t count)
    {
        (void)destructor; // only for the cleanup after a constructor that throws
        constructElements(array.address, constructor.address, size, count);
    }

    // Constructs an array that operator new made: a 16 byte header holds the element size (+0) and count (+4), the elements follow it.
    // Returns the first element, or 0 when the block is null.
    GuestPtr<void> sdk_construct_new_array(GuestPtr<void> block, GuestPtr<void> constructor, GuestPtr<void> destructor, uint32_t size, uint32_t count)
    {
        (void)destructor;
        if (!block)
            return GuestPtr<void>{};
        port::store<uint32_t>(block.address, size);
        port::store<uint32_t>(block.address + 4u, count);
        const GuestPtr<void> first{block.address + 0x10u};
        if (constructor)
            constructElements(first.address, constructor.address, size, count);
        return first;
    }

    // Puts a global object on the chain (the record `node` is the caller's: next, destructor, object); returns the object.
    GuestPtr<void> sdk_mw_register_global_object(GuestPtr<void> object, GuestPtr<void> destructor, GuestPtr<void> node)
    {
        port::store<uint32_t>(node.address, port::load<uint32_t>(kGlobalChain));
        port::store<uint32_t>(node.address + 4u, destructor.address);
        port::store<uint32_t>(node.address + 8u, object.address);
        port::store<uint32_t>(kGlobalChain, node.address);
        return object;
    }

    // Destroys the registered global objects, the last registered first: each destructor gets the object and -1.
    void sdk_destroy_global_chain()
    {
        while (const uint32_t node = port::load<uint32_t>(kGlobalChain))
        {
            port::store<uint32_t>(kGlobalChain, port::load<uint32_t>(node));
            port::callGuest<void>(port::load<uint32_t>(node + 4u), port::load<uint32_t>(node + 8u), -1);
        }
    }

    // The entry the start-up code calls to run the global destructors.
    void sdk_destroy_global_chain_stub() { sdk_destroy_global_chain(); }

    // Calls every function of a table of pointers, in order: the static constructors.
    void sdk_initialize_cpp_rts(GuestPtr<void> first, GuestPtr<void> last)
    {
        for (uint32_t at = first.address; at < last.address; at += 4u)
            port::callGuest<void>(port::load<uint32_t>(at));
    }

    // The entry the start-up code calls to run the static constructors listed at 0x1BCDD0.
    void sdk_crt0_InitCpp() { sdk_initialize_cpp_rts(GuestPtr<void>{kStaticInitFirst}, GuestPtr<void>{kStaticInitLast}); }

    PORT_FN(sdk_operator_new);
    PORT_FN(sdk_operator_delete);
    PORT_FN(sdk_mw_NewArray);
    PORT_FN(sdk_mw_DeleteArray);
    PORT_FN(sdk_construct_array);
    PORT_FN(sdk_construct_new_array);
    PORT_FN(sdk_mw_register_global_object);
    PORT_FN(sdk_destroy_global_chain);
    PORT_FN(sdk_destroy_global_chain_stub);
    PORT_FN(sdk_initialize_cpp_rts);
    PORT_FN(sdk_crt0_InitCpp);
}
