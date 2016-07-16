/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2016 John Baier <ebusd@ebusd.eu>, Roland Jax 2012-2014 <ebusd@liwest.at>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "thread.h"


Thread::~Thread()
{
	if (m_started) {
		pthread_cancel(m_thread.native_handle());
		pthread_detach(m_thread.native_handle());
	}
}

void Thread::setName(const string& name)
{
#ifdef HAVE_PTHREAD_SETNAME_NP
#ifndef __MACH__
		pthread_setname_np(m_thread.native_handle(), name.c_str());
#endif
#endif
}

bool Thread::start(const char* name)
{
	try {
		m_thread = std::thread(std::bind(&Thread::enter, this));
		setName(name);
		m_started = true;
		return true;
	}
	catch(const std::exception& e)
	{
		m_started = false;
		return false;
	}
}

bool Thread::join()
{
	int result = -1;

	try {
		if (m_started) {
			m_stopped = true;
			m_thread.join();
			m_started = false;
		}
	}
	catch(const std::exception& e)
	{
	}

	return result == 0;
}

void Thread::enter() {
	m_running = true;
	run();
	m_running = false;
}

void WaitThread::stop()
{
	m_mutex.lock();
	m_cond.notify_one();
	m_mutex.unlock();

	Thread::stop();
}

bool WaitThread::join()
{
	m_mutex.lock();
	m_cond.notify_one();
	m_mutex.unlock();

	return Thread::join();
}

bool WaitThread::Wait(int seconds)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_cond.wait_for(lock, std::chrono::seconds(seconds));

	return isRunning();
}
