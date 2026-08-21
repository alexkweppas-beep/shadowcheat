FROM node:20-alpine
WORKDIR /app
COPY package*.json ./
RUN apk add --no-cache python3 make g++ && npm ci --only=production
COPY . .
# create products dir if not exists
RUN mkdir -p products/rage products/altv
EXPOSE 3001
CMD ["npm", "start"]
