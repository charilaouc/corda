package net.corda.node.services.persistence

import net.corda.testing.internal.configureDatabase
import net.corda.testing.node.MockServices.Companion.makeTestDataSourceProperties
import org.junit.After
import org.junit.Test
import kotlin.test.assertEquals

class TransactionCallbackTest {
    private val database = configureDatabase(makeTestDataSourceProperties(), { null }, { null })

    @After
    fun closeDatabase() {
        database.close()
    }

    @Test(timeout=300_000)
	fun `onCommit called and onRollback not called on commit`() {
        var onCommitCount = 0
        var onRollbackCount = 0
        database.transaction {
            onCommit { onCommitCount++ }
            onRollback { onRollbackCount++ }
        }
        assertEquals(1, onCommitCount)
        assertEquals(0, onRollbackCount)
    }

    @Test(timeout=300_000)
	fun `onCommit not called and onRollback called on rollback`() {
        class TestException : Exception()

        var onCommitCount = 0
        var onRollbackCount = 0
        try {
            database.transaction {
                onCommit { onCommitCount++ }
                onRollback { onRollbackCount++ }
                throw TestException()
            }
        } catch (e: TestException) {
        }
        assertEquals(0, onCommitCount)
        assertEquals(1, onRollbackCount)
    }
}