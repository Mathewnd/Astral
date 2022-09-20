#include <kernel/fd.h>
#include <arch/spinlock.h>
#include <stdbool.h>
#include <kernel/alloc.h>

// XXX limit the max fd number

int fd_release(fd_t* fd){	
	spinlock_release(&fd->lock);
	return 0;
}

int fd_access(fdtable_t* fdtable, fd_t** fd, int ifd){
	spinlock_acquire(&fdtable->lock);

	if(ifd >= fdtable->fdcount || fdtable->fd[ifd] == NULL || fdtable->fd[ifd]->node == NULL){
		spinlock_release(&fdtable->lock);
		return EBADF;
	}

	*fd = fdtable->fd[ifd];
	fd_t* efd = *fd;
	spinlock_acquire(&efd->lock);
	spinlock_release(&fdtable->lock);

	return 0;

}

int fd_alloc(fdtable_t* fdtable, fd_t** fd, int* ifd){
	spinlock_acquire(&fdtable->lock);
	
	// find fd to use
	
	bool ok = false;

	for(uintmax_t i = fdtable->firstfreefd; i < fdtable->fdcount; ++i){
		if(!fdtable->fd[i]){
			fdtable->firstfreefd = i;
			*ifd = i;
			ok = true;
			break;
		}
	}

	// resize table

	if(!ok){
		
		fd_t** tmp = realloc(fdtable->fd, sizeof(fd_t*)*(fdtable->fdcount + 1));
		
		if(!tmp){
			spinlock_release(&fdtable->lock);
			return ENOMEM;
		}	

		fdtable->fd = tmp;
		*ifd = fdtable->fdcount;
		++fdtable->fdcount;
		
	}
	
	fd_t* tmp = alloc(sizeof(fd_t));
	
	if(!tmp){
		spinlock_release(&fdtable->lock);
		return ENOMEM;
	}	

	spinlock_acquire(&tmp->lock);
	tmp->refcount = 1;


	fdtable->fd[*ifd] = tmp;

	spinlock_release(&fdtable->lock);

	*fd = tmp;

	return 0;

}

int fd_free(fdtable_t* fdtable, int ifd){
	spinlock_acquire(&fdtable->lock);	

	if(ifd >= fdtable->fdcount || fdtable->fd[ifd] == NULL){
		spinlock_release(&fdtable->lock);
		return EBADF;
	}

	fd_t* fd = fdtable->fd[ifd];
	spinlock_acquire(&fd->lock);

	if(--fd->refcount > 0){
		fdtable->fd[ifd] = NULL;
		spinlock_release(&fd->lock);
		spinlock_release(&fdtable->lock);
		return 0;
	}

	fdtable->fd[ifd] = NULL;
	
	spinlock_release(&fdtable->lock);
	int err = 0;
	if(fd->node)
		err = vfs_close(fd->node);
	
	free(fd);

	return err;
	
}

int fd_tableclone(fdtable_t* source, fdtable_t* dest){

	spinlock_acquire(&source->lock);

	if(fd_tableinit(dest)){
		spinlock_release(&source->lock);
		return ENOMEM;
	}
	
	if(source->fdcount != dest->fdcount){
		void* tmp = realloc(dest->fd, source->fdcount*sizeof(fd_t*));
		if(!tmp){
			spinlock_release(&source->lock);
			return ENOMEM;
		}
		dest->fd = tmp;
		dest->fdcount = source->fdcount;
	}

	
	for(uintmax_t i = 0; i < dest->fdcount; ++i){
		if(!source->fd[i])
			continue;

		dest->fd[i] = source->fd[i];

		spinlock_acquire(&dest->fd[i]->lock);

		++dest->fd[i]->refcount;

		spinlock_release(&dest->fd[i]->lock);
		
	}


	spinlock_release(&source->lock);
	

}

int fd_tableinit(fdtable_t* fdtable){
	
	fdtable->fd = alloc(sizeof(fd_t*)*3);

	if(!fdtable->fd) return ENOMEM;

	fdtable->fdcount = 3;
	
	return 0;

}
