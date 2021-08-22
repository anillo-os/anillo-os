.text
.global farch_threads_runner
farch_threads_runner:
	// rdi holds user data for the initializer
	// r10 holds the initializer

	// rdi already holds the user data
	call *%r10

	// time to die
	call fthread_kill_self
	ud2 // this function should never return
