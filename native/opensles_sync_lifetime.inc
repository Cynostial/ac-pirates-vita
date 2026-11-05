/* A sync worker must retain each object from the engine lookup through the
 * last transport/refill access. Leases live on worker stacks, so this adds no
 * per-player allocations and does not change the installed OpenSL object ABI.
 * Lock order: engine -> lease mutex; never hold either while locking a player.
 */
typedef struct AcpSyncLease {
    IObject *object;
    pthread_t owner;
    int deferred_destroy;
    struct AcpSyncLease *next;
} AcpSyncLease;
static pthread_mutex_t acp_sync_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t acp_sync_changed = PTHREAD_COND_INITIALIZER;
static AcpSyncLease *acp_sync_readers;

static IObject *acp_sync_acquire(CEngine *engine, unsigned index, AcpSyncLease *lease)
{
    object_lock_exclusive(&engine->mObject);
    IObject *object = engine->mEngine.mInstances[index];
    if (object) {
        lease->object = object;
        lease->owner = pthread_self();
        lease->deferred_destroy = 0;
        pthread_mutex_lock(&acp_sync_mutex);
        lease->next = acp_sync_readers;
        acp_sync_readers = lease;
        pthread_mutex_unlock(&acp_sync_mutex);
    }
    object_unlock_exclusive(&engine->mObject);
    return object;
}

/* Destruction from a sync callback completes after that callback's last
 * access. Waiting on our own lease would deadlock. */
int acp_sync_defer_destroy(IObject *object)
{
    int deferred = 0;
    pthread_mutex_lock(&acp_sync_mutex);
    for (AcpSyncLease *p = acp_sync_readers; p; p = p->next) {
        if (p->object == object && pthread_equal(p->owner, pthread_self())) {
            p->deferred_destroy = deferred = 1;
            break;
        }
    }
    pthread_mutex_unlock(&acp_sync_mutex);
    return deferred;
}

static void acp_sync_release(AcpSyncLease *lease)
{
    pthread_mutex_lock(&acp_sync_mutex);
    AcpSyncLease **p = &acp_sync_readers;
    while (*p && *p != lease) p = &(*p)->next;
    assert(*p == lease);
    *p = lease->next;
    int deferred = lease->deferred_destroy;
    IObject *object = lease->object;
    pthread_cond_broadcast(&acp_sync_changed);
    pthread_mutex_unlock(&acp_sync_mutex);
    if (deferred) IObject_Destroy((SLObjectItf)object);
}

/* Called after removal from mInstances, with the object's mutex locked.
 * Release that mutex while waiting so an already leased worker can finish. */
void acp_sync_wait_locked(IObject *object)
{
    object_unlock_exclusive(object);
    pthread_mutex_lock(&acp_sync_mutex);
    for (;;) {
        AcpSyncLease *p = acp_sync_readers;
        while (p && p->object != object) p = p->next;
        if (!p) break;
        pthread_cond_wait(&acp_sync_changed, &acp_sync_mutex);
    }
    pthread_mutex_unlock(&acp_sync_mutex);
    object_lock_exclusive(object);
}
