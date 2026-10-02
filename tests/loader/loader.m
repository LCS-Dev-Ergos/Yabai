// Exercise the production loader with Mach calls mocked: no Dock task is opened.
#include <Cocoa/Cocoa.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <assert.h>
#include <string.h>

static int allocations, deallocations, port_deallocations, terminations, polls;
static bool sentinel, fail_write, fail_conversion;

static kern_return_t mock_task_for_pid(mach_port_name_t self, int pid, mach_port_name_t *task)
{
    *task = 1;
    return KERN_SUCCESS;
}
static kern_return_t mock_allocate(vm_map_t task, mach_vm_address_t *address, mach_vm_size_t size, int flags)
{
    *address = 0x100000 * ++allocations;
    return KERN_SUCCESS;
}
static kern_return_t mock_write(vm_map_t task, mach_vm_address_t address, vm_offset_t data,
                                mach_msg_type_number_t size)
{
    return fail_write ? KERN_FAILURE : KERN_SUCCESS;
}
static kern_return_t mock_protect(vm_map_t task, vm_address_t address, vm_size_t size, boolean_t maximum,
                                  vm_prot_t protection)
{
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_create(task_t task, thread_act_t *thread)
{
    *thread = 2;
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_running(task_t task, thread_state_flavor_t flavor, thread_state_t state,
                                         mach_msg_type_number_t count, thread_act_t *thread)
{
    *thread = 3;
    return KERN_SUCCESS;
}
static kern_return_t mock_convert(thread_act_t thread, int direction, thread_state_flavor_t flavor,
                                  thread_state_t input, mach_msg_type_number_t count, thread_state_t output,
                                  mach_msg_type_number_t *output_count)
{
    memcpy(output, input, count * sizeof(natural_t));
    return fail_conversion ? KERN_FAILURE : KERN_SUCCESS;
}
static kern_return_t mock_thread_state(thread_act_t thread, thread_state_flavor_t flavor,
                                       thread_state_t state, mach_msg_type_number_t count)
{
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_resume(thread_act_t thread)
{
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_get_state(thread_act_t thread, thread_state_flavor_t flavor,
                                           thread_state_t state, mach_msg_type_number_t *count)
{
    ++polls;
    ((arm_thread_state64_t *)state)->__x[0] = sentinel ? 0x79616265 : 0;
    return KERN_SUCCESS;
}
static kern_return_t mock_terminate(thread_act_t thread)
{
    ++terminations;
    return KERN_SUCCESS;
}
static kern_return_t mock_deallocate(vm_map_t task, mach_vm_address_t address, mach_vm_size_t size)
{
    ++deallocations;
    return KERN_SUCCESS;
}
static kern_return_t mock_port_deallocate(ipc_space_t task, mach_port_name_t port)
{
    ++port_deallocations;
    return KERN_SUCCESS;
}
static void *mock_dlopen(const char *path, int flags)
{
    return (void *)1;
}
static int mock_dlclose(void *handle)
{
    return 0;
}
static void *mock_dlsym(void *handle, const char *name)
{
    return strcmp(name, "thread_convert_thread_state") == 0 ? (void *)&mock_convert : (void *)0x1000;
}
static int mock_usleep(useconds_t delay)
{
    return 0;
}

#define task_for_pid mock_task_for_pid
#define mach_vm_allocate mock_allocate
#define mach_vm_write mock_write
#define vm_protect mock_protect
#define thread_create mock_thread_create
#define thread_create_running mock_thread_running
#define thread_set_state mock_thread_state
#define thread_resume mock_thread_resume
#define thread_get_state mock_thread_get_state
#define thread_terminate mock_terminate
#define mach_vm_deallocate mock_deallocate
#define mach_port_deallocate mock_port_deallocate
#define dlopen mock_dlopen
#define dlclose mock_dlclose
#define dlsym mock_dlsym
#define usleep mock_usleep
#define main loader_cli_main
#include "../../src/osax/loader.m"
#undef main

static void reset_mocks(void)
{
    allocations = deallocations = port_deallocations = terminations = polls = 0;
    sentinel = fail_write = fail_conversion = false;
}

int main(void)
{
    reset_mocks();
    assert(loader_inject(999) == 1);
    assert(polls == 10 && terminations >= 1);
    assert(deallocations == 1 && port_deallocations >= 2);

    reset_mocks();
    sentinel = true;
    assert(loader_inject(999) == 0);
    assert(polls == 1 && deallocations == 1);

    reset_mocks();
    fail_write = true;
    assert(loader_inject(999) == 1);
    assert(allocations == 1 && deallocations == 1 && terminations == 0);
    assert(port_deallocations == 1);

    reset_mocks();
    fail_conversion = true;
    assert(loader_inject(999) == 1);
    assert(allocations == 2 && deallocations == 2 && terminations == 1);

    puts("loader: timeout, sentinel, early failure and conversion cleanup passed");
    return 0;
}
