const test = require('node:test');
const assert = require('node:assert/strict');
const jwt = require('jsonwebtoken');
const delivery = require('./product_delivery');

const secret = 'test-secret-with-at-least-thirty-two-bytes';
const hwid = 'hwid-123';

function ticketInput(overrides = {}) {
    return {
        userId: 7,
        productId: 'ragemp',
        hwid,
        customerTag: delivery.deriveCustomerTag(7, hwid, secret),
        integrityHash: 'a'.repeat(64),
        launchId: 'launch-a',
        variantId: 3,
        jwtSecret: secret,
        ...overrides
    };
}

test('personalizes exactly one protected artifact placeholder', () => {
    const tag = delivery.deriveCustomerTag(7, hwid, secret);
    const raw = Buffer.concat([
        Buffer.from('prefix'),
        delivery.CUSTOMER_TAG_PLACEHOLDER,
        Buffer.from('suffix')
    ]);
    const output = delivery.personalizeArtifact(raw, tag);
    assert.equal(output.includes(Buffer.from(tag)), true);
    assert.equal(output.includes(delivery.CUSTOMER_TAG_PLACEHOLDER), false);
});

test('rejects missing or duplicate customer placeholders', () => {
    assert.throws(() => delivery.personalizeArtifact(Buffer.from('none'), 'x'));
    const duplicate = Buffer.concat([
        delivery.CUSTOMER_TAG_PLACEHOLDER,
        delivery.CUSTOMER_TAG_PLACEHOLDER
    ]);
    const tag = delivery.deriveCustomerTag(7, hwid, secret);
    assert.throws(() => delivery.personalizeArtifact(duplicate, tag));
});

test('verifies product, hwid, artifact and active launch id', () => {
    const input = ticketInput();
    const token = delivery.issueLaunchTicket(input);
    const claims = delivery.verifyLaunchTicket(token, {
        productId: input.productId,
        hwid,
        customerTag: input.customerTag,
        integrityHash: input.integrityHash,
        launchId: input.launchId,
        variantId: input.variantId
    }, secret);
    assert.equal(claims.uid, input.userId);
});

test('assigns a stable cohort within the configured range', () => {
    const request = {
        userId: 7,
        hwid,
        variantSecret: secret,
        variantCount: 32
    };
    const first = delivery.deriveVariantId(request);
    assert.equal(first, delivery.deriveVariantId(request));
    assert.ok(first >= 0 && first < request.variantCount);
});

test('rejects expired, foreign and superseded tickets', () => {
    const input = ticketInput();
    const expired = jwt.sign({
        uid: input.userId,
        scope: 'product',
        product_id: input.productId
    }, secret, { expiresIn: -1 });
    assert.throws(() => delivery.verifyLaunchTicket(expired, {}, secret));
    const token = delivery.issueLaunchTicket(input);
    assert.throws(() => delivery.verifyLaunchTicket(
        token,
        { productId: 'external' },
        secret
    ));
    assert.throws(() => delivery.verifyLaunchTicket(
        token,
        { launchId: 'newer-launch' },
        secret
    ));
});
