import React, { useState } from 'react';
import './AuthModal.css';
import { API_BASE } from '../api.js';

export default function AuthModal({ isOpen, onClose, onLogin }) {
  const [mode, setMode] = useState('login');
  const [login, setLogin] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [passwordConfirm, setPasswordConfirm] = useState('');
  const [status, setStatus] = useState('idle');

  if (!isOpen) return null;

  const handleSubmit = async (e) => {
    e.preventDefault();
    setStatus('loading');
    if (mode === 'login' && login && password) {
      try {
        const res = await fetch(`${API_BASE}/api/auth/login`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            username: login,
            password: password,
            isWeb: true,
            hwid: 'dummy-browser-hwid',
            timestamp: Date.now().toString()
          })
        });
        const data = await res.json();
        if (data.token) {
          setStatus('success');
          localStorage.setItem('token', data.token);
          onLogin();
        } else {
          setStatus('error');
          alert(data.error || 'Не удалось войти. Проверьте логин и пароль.');
        }
      } catch (err) {
        setStatus('error');
        alert('Сервер не ответил. Попробуйте ещё раз.');
      }
    } else if (mode === 'register') {
      try {
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
          setStatus('success');
          alert('Аккаунт создан. Теперь войдите.');
          setMode('login');
          setStatus('idle');
        } else {
          setStatus('error');
          alert(data.error || 'Не удалось создать аккаунт.');
        }
      } catch (err) {
        setStatus('error');
        alert('Сервер не ответил. Попробуйте ещё раз.');
      }
    } else if (mode === 'forgot') {
      alert('Восстановление временно недоступно');
      setStatus('idle');
      setMode('login');
    }
  };

  const renderLogin = () => (
    <>
      <h2 className="auth-title">Войти в shadow</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <label htmlFor="auth-login">Логин</label>
          <input
            id="auth-login"
            type="text"
            placeholder="имя пользователя"
            value={login}
            onChange={e => setLogin(e.target.value)}
            required
            autoComplete="username"
            aria-invalid={status === 'error'}
          />
        </div>
        <div className="auth-input-group">
          <label htmlFor="auth-password">Пароль</label>
          <input
            id="auth-password"
            type="password"
            placeholder="••••••••"
            value={password}
            onChange={e => setPassword(e.target.value)}
            required
            autoComplete="current-password"
          />
        </div>
        <button type="submit" className="auth-submit-btn" data-state={status === 'loading' ? 'loading' : undefined} disabled={status === 'loading'}>
          {status === 'loading' ? 'Входим…' : 'Войти'}
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row">
          <span className="auth-text-muted">Нет аккаунта? </span>
          <button type="button" className="auth-link-bold" onClick={() => { setMode('register'); setStatus('idle'); }}>Создать аккаунт</button>
        </div>
        <div className="auth-link-row">
          <button type="button" className="auth-link-bold" onClick={() => { setMode('forgot'); setStatus('idle'); }}>Забыли пароль?</button>
        </div>
      </div>
    </>
  );

  const renderRegister = () => (
    <>
      <h2 className="auth-title">Создание аккаунта</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <label htmlFor="reg-login">Логин</label>
          <input id="reg-login" type="text" placeholder="имя пользователя" value={login} onChange={e => setLogin(e.target.value)} required />
        </div>
        <div className="auth-input-group">
          <label htmlFor="reg-email">Электронная почта</label>
          <input id="reg-email" type="email" placeholder="name@domain.com" value={email} onChange={e => setEmail(e.target.value)} required />
        </div>
        <div className="auth-input-group">
          <label htmlFor="reg-pass">Пароль</label>
          <input id="reg-pass" type="password" placeholder="••••••••" value={password} onChange={e => setPassword(e.target.value)} required />
        </div>
        <div className="auth-input-group">
          <label htmlFor="reg-pass2">Повторите пароль</label>
          <input id="reg-pass2" type="password" placeholder="••••••••" value={passwordConfirm} onChange={e => setPasswordConfirm(e.target.value)} required />
        </div>
        <button type="submit" className="auth-submit-btn" data-state={status === 'loading' ? 'loading' : undefined} disabled={status === 'loading'}>
          {status === 'loading' ? 'Создаём…' : 'Создать аккаунт'}
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row">
          <span className="auth-text-muted">Уже есть аккаунт? </span>
          <button type="button" className="auth-link-bold" onClick={() => { setMode('login'); setStatus('idle'); }}>Войти</button>
        </div>
      </div>
    </>
  );

  const renderForgot = () => (
    <>
      <h2 className="auth-title">Восстановление аккаунта</h2>
      <form onSubmit={handleSubmit}>
        <div className="auth-input-group">
          <label htmlFor="forgot-login">Логин или почта</label>
          <input id="forgot-login" type="text" placeholder="имя или name@domain.com" value={login} onChange={e => setLogin(e.target.value)} required />
        </div>
        <button type="submit" className="auth-submit-btn">
          Отправить ссылку
        </button>
      </form>
      <div className="auth-links">
        <div className="auth-link-row">
          <span className="auth-text-muted">Вспомнили пароль? </span>
          <button type="button" className="auth-link-bold" onClick={() => setMode('login')}>Войти</button>
        </div>
      </div>
    </>
  );

  return (
    <div className="auth-modal-overlay" onClick={onClose}>
      <div className="auth-modal" onClick={e => e.stopPropagation()} role="dialog" aria-modal="true">
        <button type="button" className="auth-close" onClick={onClose} aria-label="Закрыть">×</button>
        <div className="auth-logo-container">
          <img src="/logo.svg" alt="shadow" className="auth-logo-icon" />
        </div>
        {mode === 'login' && renderLogin()}
        {mode === 'register' && renderRegister()}
        {mode === 'forgot' && renderForgot()}
      </div>
    </div>
  );
}
