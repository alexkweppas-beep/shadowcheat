const test = require('node:test');
const assert = require('node:assert/strict');
const delivery = require('./product_delivery');

const secret = 'feature-test-secret-with-thirty-two-bytes';

function canaryInput() {
    return {
        userId: 17,
        launchId: 'launch-feature-a',
        featureId: 'quant',
        watermarkSecret: secret
    };
}

test('feature canary is stable for one launch and feature', () => {
    const input = canaryInput();
    const first = delivery.deriveFeatureCanary(input);
    assert.equal(first, delivery.deriveFeatureCanary(input));
    assert.notEqual(first, delivery.deriveFeatureCanary({
        ...input,
        featureId: 'five_rp'
    }));
});

test('watermarked feature bundle decrypts without a reusable ciphertext', () => {
    const source = Buffer.from('globalThis.featureLoaded=true;');
    const canary = delivery.deriveFeatureCanary(canaryInput());
    const watermarked = delivery.watermarkFeatureSource(source, canary);
    const first = delivery.encryptFeatureBundle(watermarked);
    const second = delivery.encryptFeatureBundle(watermarked);
    assert.notEqual(first.payload, second.payload);
    const restored = delivery.decryptFeatureBundle(first);
    assert.equal(restored.includes(source), true);
    assert.equal(restored.includes(Buffer.from(canary)), true);
});

test('combat policy is a fixed binary envelope with a session canary', () => {
    const policy = delivery.combatPolicyBuffer();
    assert.equal(policy.length, 116);
    assert.equal(policy.readFloatLE(0), 1.350000023841858);
    assert.equal(policy.readUInt32LE(112), 16);
    const canary = delivery.deriveFeatureCanary({
        ...canaryInput(),
        featureId: 'policy'
    });
    const marked = delivery.watermarkPolicy(policy, canary);
    assert.equal(marked.subarray(0, policy.length).equals(policy), true);
    assert.equal(marked.includes(Buffer.from(canary)), true);
});
