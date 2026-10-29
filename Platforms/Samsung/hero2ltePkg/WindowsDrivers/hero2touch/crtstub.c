/* Minimal replacements for the WDK GS/CFG support library (its objects carry
 * .impcall metadata that lld-link cannot process for kernel images). */
typedef unsigned long long UINT_PTR;
UINT_PTR __security_cookie = 0x00002B992DDFA232ULL;
UINT_PTR __security_cookie_complement = ~0x00002B992DDFA232ULL;
void __security_init_cookie(void) { }
void __security_check_cookie(UINT_PTR cookie) { (void)cookie; }
void _guard_check_icall(void *target) { (void)target; }
