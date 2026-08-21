import React, { useState } from 'react';
import './AuthModal.css';
import { API_BASE } from '../api.js';

export default function AuthModal({ isOpen, onClose, onLogin }) {
  const [mode, setMode] = useState('login'); // 'login', 'register', 'forgot'
  const [login, setLogin] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [passwordConfirm, setPasswordConfirm] = useState('');

  if (!isOpen) return null;

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (mode === 'login' && login && password) {
      try {
        const res = await fetch(`${API_BASE}/api/auth/login`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            username: login,
            password: password,
            isWeb: true,
            hwid: 'dummy-browser-hwid', // Browsers don't have HWID, just mock for now
            timestamp: Date.now().toString()
          })
        });
        const data = await res.json();
        if (data.token) {
          localStorage.setItem('token', data.token);
          onLogin();
        } else {
          alert(data.error || 'Login failed');
        }
      } catch (err) {
        alert('Server error');
      }
    } else if (mode === 'register') {
      try {
        // Since register requires admin in current backend, we might just mock or use real endpoint.
        // The backend requires requireAdmin. Let's send a request anyway.
        const res = await fetch(`${API_BASE}/api/auth/register`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${localStorage.getItem('token') || ''}`
          },
          body: JSON.stringify({ username: login, password: password, email: email })
        });
        const data = await res.json();
        if (data.ok) {
          alert('Registered successfully! Now login.');
          setMode('login');
        } else {
          alert(data.error || 'Registration failed');
        }
      } catch (err) {
        alert('Server error');
      }
    } else if (mode === 'forgot') {
      alert('Временно недоступно');
      setMode('login');
    }
  };

  const renderLogin = () => (
    <>
      <h2 className="auth-title">Войти в shadow cheats</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <input
            type="text"
            placeholder="Логин"
            value={login}
            onChange={e => setLogin(e.target.value)}
            required
          />
        </div>
        <div className="auth-input-group">
          <input
            type="password"
            placeholder="Пароль"
            value={password}
            onChange={e => setPassword(e.target.value)}
            required
          />
        </div>
        <button type="submit" className="auth-submit-btn">
          Войти
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row">
          <span className="auth-text-muted">Нет аккаунта? </span>
          <span className="auth-link-bold" onClick={() => setMode('register')}>Создать аккаунт</span>
        </div>
        <div className="auth-link-row">
          <span className="auth-link-bold" onClick={() => setMode('forgot')}>Забыли пароль?</span>
        </div>
      </div>
    </>
  );

  const renderRegister = () => (
    <>
      <h2 className="auth-title">Создание аккаунта</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <input
            type="text"
            placeholder="Логин"
            value={login}
            onChange={e => setLogin(e.target.value)}
            required
          />
        </div>
        <div className="auth-input-group">
          <input
            type="email"
            placeholder="Email address"
            value={email}
            onChange={e => setEmail(e.target.value)}
            required
          />
        </div>
        <div className="auth-input-group">
          <input
            type="password"
            placeholder="Пароль"
            value={password}
            onChange={e => setPassword(e.target.value)}
            required
          />
        </div>
        <div className="auth-input-group">
          <input
            type="password"
            placeholder="Повторите пароль"
            value={passwordConfirm}
            onChange={e => setPasswordConfirm(e.target.value)}
            required
          />
        </div>
        <button type="submit" className="auth-submit-btn">
          Создать аккаунт
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row">
          <span className="auth-text-muted">Уже есть аккаунт? </span>
          <span className="auth-link-bold" onClick={() => setMode('login')}>Войти</span>
        </div>
      </div>
    </>
  );

  const renderForgot = () => (
    <>
      <h2 className="auth-title">Восстановление аккаунта</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <input
            type="text"
            placeholder="Логин или email"
            value={login}
            onChange={e => setLogin(e.target.value)}
            required
          />
        </div>
        <button type="submit" className="auth-submit-btn">
          Отправить ссылку на восстановление
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row" style={{ marginTop: '16px' }}>
          <span className="auth-text-muted">Вспомнили пароль? </span>
          <span className="auth-link-bold" onClick={() => setMode('login')}>Войти</span>
        </div>
      </div>
    </>
  );

  return (
    <div className="auth-modal-overlay" onClick={onClose}>
      <div className="auth-modal" onClick={e => e.stopPropagation()}>
        <div className="auth-logo-container">
          <img src="/logo.svg" alt="Logo" className="auth-logo-icon" />
        </div>

        {mode === 'login' && renderLogin()}
        {mode === 'register' && renderRegister()}
        {mode === 'forgot' && renderForgot()}

      </div>
    </div>
  );
}
