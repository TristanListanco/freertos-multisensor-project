/* Workarounds for the Wokwi Blue Pill CPU model, which differs from a real
   Cortex-M3 in how it takes exceptions:
     - A store that pends PendSV from task code takes PendSV immediately and
       saves the store itself as the return address, so the task re-runs the
       store when it resumes, pends PendSV again and never gets any further.
     - PRIMASK (cpsid i) masks nothing, and svc does nothing, so neither can
       be used to hold PendSV off until after the store.
   On real hardware PendSV never returns onto that store, so this code only
   changes behaviour in the simulator. */

void xPortPendSVHandler(void);

/* Replaces the FreeRTOS port's vPortYield() at link time
   (-Wl,--wrap=vPortYield in extra_script.py). Same as the stock version, but
   the store that pends PendSV carries a label PendSV_Handler can recognise. */
void __wrap_vPortYield(void)
{
    __asm volatile(
        "   ldr  r0, =0xE000ED04        \n" /* SCB->ICSR */
        "   ldr  r1, =0x10000000        \n" /* PENDSVSET */
        "   .global wokwi_yield_store   \n"
        "wokwi_yield_store:             \n"
        "   str  r1, [r0]               \n"
        "   dsb                         \n"
        "   isb                         \n"
        ::: "r0", "r1", "memory");
}

/* If PendSV interrupted a task on the yield store, resume that task after the
   store instead of on it, then run the port's context switch. */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "   mrs  r0, psp                \n"
        "   ldr  r1, [r0, #24]          \n" /* stacked PC */
        "   ldr  r2, =wokwi_yield_store \n"
        "   cmp  r1, r2                 \n"
        "   bne  1f                     \n"
        "   adds r1, #2                 \n" /* the store is a 16-bit instruction */
        "   str  r1, [r0, #24]          \n"
        "1: b    xPortPendSVHandler     \n");
}
